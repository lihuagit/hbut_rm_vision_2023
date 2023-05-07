#include "trt_armor_detector/trt_detector.h"

void  get_d2i_matrix(affine_matrix &afmt,cv::Size to,cv::Size from)
{
    float scale = std::min(to.width/float(from.width),to.height/float(from.height));
    afmt.i2d[0]=scale;
    afmt.i2d[1]=0;
    afmt.i2d[2]=-scale*from.width*0.5+to.width*0.5;
    afmt.i2d[3]=0;
    afmt.i2d[4]=scale;
    afmt.i2d[5]=-scale*from.height*0.5+to.height*0.5;

    cv::Mat mat_i2d(2,3,CV_32F,afmt.i2d);
    cv::Mat mat_d2i(2,3,CV_32F,afmt.d2i);
    cv::invertAffineTransform(mat_i2d,mat_d2i);
    memcpy(afmt.d2i,mat_d2i.ptr<float>(0),sizeof(afmt.d2i));
}



TRTDetector::TRTDetector(): input_blob_name("input"), output_blob_name("output")
{
    runtime=nullptr;
    engine=nullptr;
    context=nullptr;
    prob=nullptr;  //trt输出 

    input_w = INPUT_W;
    input_h = INPUT_H;

    img_host = nullptr;
    img_device = nullptr;
    affine_matrix_host=nullptr;
    affine_matrix_device=nullptr;
    decode_ptr_host=nullptr;
    decode_ptr_device=nullptr;

	label_map[0] = "G";
	label_map[1] = "1";
	label_map[2] = "2";
	label_map[3] = "3";
	label_map[4] = "4";
	label_map[5] = "5";
	label_map[6] = "O";
	label_map[7] = "Bs";
	label_map[8] = "Bb";
}

void TRTDetector::loadTrtModel(const char *trtmodel)
{
	Logger gLogger;
	char *trtModelStream{nullptr};
	size_t size{0};
	const std::string engine_file_path {trtmodel};
	std::ifstream file(engine_file_path, std::ios::binary);
	if (file.good()) {
		file.seekg(0, file.end);
		size = file.tellg();
		file.seekg(0, file.beg);
		trtModelStream = new char[size];
		assert(trtModelStream);
		file.read(trtModelStream, size);
		file.close();
	}

    runtime = createInferRuntime(gLogger);
    assert(runtime != nullptr);
    engine = runtime->deserializeCudaEngine(trtModelStream, size);
    assert(engine != nullptr); 
    context = engine->createExecutionContext();
    assert(context != nullptr);

    auto out_dims = engine->getBindingDimensions(1);
    output_size = 1;
    output_candidates = out_dims.d[1];

	for(int j=0;j<out_dims.nbDims;j++) 
		output_size *= out_dims.d[j];

    const int inputIndex = engine->getBindingIndex(input_blob_name);
    const int outputIndex = engine->getBindingIndex(output_blob_name);
    assert(inputIndex == 0);
    assert(outputIndex == 1);

    CHECK(cudaMalloc((void**)&buffers[inputIndex],  3 * input_h * input_w * sizeof(float)));  //trt输入内存申请
    CHECK(cudaMalloc((void**)&buffers[outputIndex], output_size * sizeof(float)));           //trt输出内存申请
    CHECK(cudaStreamCreate(&stream));

	decode_ptr_host = new float[1+MAX_OBJECTS*NUM_BOX_ELEMENT];

    // prepare input data cache in pinned memory
    CHECK(cudaMallocHost((void**)&affine_matrix_host,sizeof(float)*6));
    CHECK(cudaMallocHost((void**)&img_host, MAX_IMAGE_INPUT_SIZE_THRESH * 3));
	
    // prepare input data cache in device memory
    CHECK(cudaMalloc((void**)&img_device, MAX_IMAGE_INPUT_SIZE_THRESH * 3));
    CHECK(cudaMalloc((void**)&affine_matrix_device,sizeof(float)*6));
    CHECK(cudaMalloc((void**)&decode_ptr_device,sizeof(float)*(1+MAX_OBJECTS*NUM_BOX_ELEMENT)));

    std::cout<<"load engine success!"<<std::endl;
    delete[] trtModelStream; 
}

void TRTDetector::detect(cv::Mat &img,std::vector<bbox> &bboxes,float prob_threshold , float nms_threshold)
{
	// std::cout<<"output_candidates:"<<output_candidates<<std::endl;
	// std::cout<<"output_size:"<<output_size<<std::endl;
	affine_matrix afmt;
	const int inputIndex = engine->getBindingIndex(input_blob_name);
	const int outputIndex = engine->getBindingIndex(output_blob_name);
	get_d2i_matrix(afmt,cv::Size(input_w,input_h),cv::Size(img.cols,img.rows));
	// double begin_time = cv::getTickCount();
	float *buffer_idx = (float*)buffers[inputIndex];
	size_t size_image = img.cols * img.rows * 3;
	// size_t size_image_dst = input_h * input_w * 3;
	memcpy(img_host, img.data, size_image);
	memcpy(affine_matrix_host,afmt.d2i,sizeof(afmt.d2i));
	CHECK(cudaMemcpyAsync(img_device, img_host, size_image, cudaMemcpyHostToDevice, stream));
	CHECK(cudaMemcpyAsync(affine_matrix_device, affine_matrix_host, sizeof(afmt.d2i), cudaMemcpyHostToDevice, stream));
	preprocess_kernel_img(img_device, img.cols, img.rows, buffer_idx, input_w, input_h, affine_matrix_device,stream);   //cuda 前处理
	// double time_pre = cv::getTickCount();
	// double time_pre_=(time_pre-begin_time)/cv::getTickFrequency()*1000;
	context->enqueueV2((void **)buffers,stream,nullptr);
	CHECK(cudaMemsetAsync(decode_ptr_device,0,sizeof(int),stream));
	float *predict =(float *) buffers[outputIndex];
	decode_kernel_invoker(predict,output_candidates,NUM_CLASSES,4,prob_threshold,affine_matrix_device,decode_ptr_device,MAX_OBJECTS,stream);  //cuda 后处理

	nms_kernel_invoker(decode_ptr_device, nms_threshold, MAX_OBJECTS, stream);//cuda nms
	
	CHECK(cudaMemcpyAsync(decode_ptr_host,decode_ptr_device,sizeof(float)*(1+MAX_OBJECTS*NUM_BOX_ELEMENT),cudaMemcpyDeviceToHost,stream));
	cudaStreamSynchronize(stream);

	int boxes_count=0;
	int count = std::min((int)*decode_ptr_host,MAX_OBJECTS);

	for (int i = 0; i<count;i++)
	{
		int basic_pos = 1+i*NUM_BOX_ELEMENT;
		int keep= decode_ptr_host[basic_pos+6];
		if( keep )
		{
			boxes_count+=1;
			bbox box;
			box.x1 =  decode_ptr_host[basic_pos+0];
			box.y1 =  decode_ptr_host[basic_pos+1];
			box.x2 =  decode_ptr_host[basic_pos+2];
			box.y2 =  decode_ptr_host[basic_pos+3];
			box.score=decode_ptr_host[basic_pos+4];
			box.label = decode_ptr_host[basic_pos+5];
			int landmark_pos = basic_pos+7;
			for (int id = 0; id<4; id+=1)
			{
				box.key_points[2*id]=decode_ptr_host[landmark_pos+2*id];
				box.key_points[2*id+1]=decode_ptr_host[landmark_pos+2*id+1];
			}
			box.color = box.label / 9;
			box.number = label_map[box.label % 9];
			double x, y;
			double light_len, light_dis;
			x = box.key_points[2*0+0] - box.key_points[2*1+0];
			y = box.key_points[2*0+1] - box.key_points[2*1+1];
			light_len = sqrt(x*x + y*y);

			x = box.key_points[2*1+0] - box.key_points[2*2+0];
			y = box.key_points[2*1+1] - box.key_points[2*2+1];
			light_dis = sqrt(x*x + y*y);

			box.ratio = (light_dis / light_len);
			bboxes.push_back(box);
		}
	}
}


TRTDetector::~TRTDetector()
{
	if (context)
		delete context;
    if (engine)
      	delete engine;
    if(runtime)
      	delete runtime;
    if (prob)
    	delete [] prob;
    delete [] decode_ptr_host;
    cudaStreamDestroy(stream);
    CHECK(cudaFree(img_device));
    CHECK(cudaFreeHost(img_host));
    CHECK(cudaFree(affine_matrix_device));
    CHECK(cudaFreeHost(affine_matrix_host));
    CHECK(cudaFree(decode_ptr_device));
    CHECK(cudaFree(buffers[0]));
    CHECK(cudaFree(buffers[1]));
}