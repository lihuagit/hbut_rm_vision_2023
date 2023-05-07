#include "trt_armor_detector/utils.hpp"

float getNorm2(float x,float y)
{
    return sqrt(x*x+y*y);
}

cv::Mat getTransForm(cv::Mat &src_img, cv::Point2f order_rect[4]) //透视变换
{
    cv::Point2f w1=order_rect[0]-order_rect[1];
    cv::Point2f w2=order_rect[2]-order_rect[3];
    auto width1 = getNorm2(w1.x,w1.y);
    auto width2 = getNorm2(w2.x,w2.y);
    auto maxWidth = std::max(width1,width2);

    cv::Point2f h1=order_rect[0]-order_rect[3];
    cv::Point2f h2=order_rect[1]-order_rect[2];
    auto height1 = getNorm2(h1.x,h1.y);
    auto height2 = getNorm2(h2.x,h2.y);
    auto maxHeight = std::max(height1,height2);
    //  透视变换
    std::vector<cv::Point2f> pts_ori(4);
    std::vector<cv::Point2f> pts_std(4);

    pts_ori[0]=order_rect[0];
    pts_ori[1]=order_rect[1];
    pts_ori[2]=order_rect[2];
    pts_ori[3]=order_rect[3];

    pts_std[0]=cv::Point2f(0,0);
    pts_std[1]=cv::Point2f(maxWidth,0);
    pts_std[2]=cv::Point2f(maxWidth,maxHeight);
    pts_std[3]=cv::Point2f(0,maxHeight);

    cv::Mat M = cv::getPerspectiveTransform(pts_ori,pts_std);
    cv:: Mat dstimg;
    cv::warpPerspective(src_img,dstimg,M,cv::Size(maxWidth,maxHeight));
    return dstimg;
}

std::string getHouZhui(std::string fileName)
{
    //    std::string fileName="/home/xiaolei/23.jpg";
    int pos=fileName.find_last_of(std::string("."));
    std::string houZui=fileName.substr(pos+1);
    return houZui;
}

int readFileList(char *basePath,std::vector<std::string> &fileList,std::vector<std::string> fileType)
{
    DIR *dir;
    struct dirent *ptr;
    char base[1000];

    if ((dir=opendir(basePath)) == NULL)
    {
        perror("Open dir error...");
        exit(1);
    }

    while ((ptr=readdir(dir)) != NULL)
    {
        if(strcmp(ptr->d_name,".")==0 || strcmp(ptr->d_name,"..")==0)    ///current dir OR parrent dir
            continue;
        else if(ptr->d_type == 8)
        {    ///file
            if (fileType.size())
            {
                std::string houZui=getHouZhui(std::string(ptr->d_name));
                for (auto &s:fileType)
                {
                    if (houZui==s)
                    {
                        fileList.push_back(basePath+std::string("/")+std::string(ptr->d_name));
                        break;
                    }
                }
            }
            else
            {
                fileList.push_back(basePath+std::string("/")+std::string(ptr->d_name));
            }
        }
        else if(ptr->d_type == 10)    ///link file
            printf("d_name:%s/%s\n",basePath,ptr->d_name);
        else if(ptr->d_type == 4)    ///dir
        {
            memset(base,'\0',sizeof(base));
            strcpy(base,basePath);
            strcat(base,"/");
            strcat(base,ptr->d_name);
            readFileList(base,fileList,fileType);
        }
    }
    closedir(dir);
    return 1;
}

void draw_rect(const cv::Mat& image, const std::vector<boundingBox>bboxes,const char* class_names[])
{
    // static const char* class_names[] = {
    //     "head", "leg", "hand", "back", "nostd", "body", "plate", "logo"};

    // cv::Mat image = bgr.clone();

    for (size_t i = 0; i < bboxes.size(); i++)
    {
        // const Object& obj = objects[i];
        const boundingBox &obj= bboxes[i];

        // fprintf(stderr, "%d = %.5f at %.2f %.2f %.2f x %.2f\n", obj.label, obj.prob,
        //         obj.rect.x, obj.rect.y, obj.rect.width, obj.rect.height);

        cv::Scalar color = cv::Scalar(color_list1[obj.label][0], color_list1[obj.label][1], color_list1[obj.label][2]);
        float c_mean = cv::mean(color)[0];
        cv::Scalar txt_color;
        if (c_mean > 0.5){
            txt_color = cv::Scalar(0, 0, 0);
        }else{
            txt_color = cv::Scalar(255, 255, 255);
        }
        cv::Rect myRect(obj.x,obj.y,obj.w,obj.h);
        cv::rectangle(image,myRect, color * 255, 2);

        char text[256];
        sprintf(text, "%s %.1f%%", class_names[obj.label], obj.score * 100);

        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, &baseLine);

        cv::Scalar txt_bk_color = color * 0.7 * 255;

        int x = obj.x;
        int y = obj.y + 1;
        //int y = obj.rect.y - label_size.height - baseLine;
        if (y > image.rows)
            y = image.rows;
        //if (x + label_size.width > image.cols)
            //x = image.cols - label_size.width;

        cv::rectangle(image, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)),
                      txt_bk_color,-1);

        cv::putText(image, text, cv::Point(x, y + label_size.height),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, txt_color, 1);
    }

}