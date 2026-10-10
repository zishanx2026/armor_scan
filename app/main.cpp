#include "frame.h"
#include "video.h"
#include <iostream>
#include <opencv2/opencv.hpp>

int main()
{
    Video video(VIDEO_PATH);
    if (!video.is_opened())
    {
        std::cout << "视频开启失败..." << std::endl;
        return -1;
    }
    else
    {
        std::cout <<"视频开启成功..." << std::endl;
    }
    while (true)
    {
        cv::Mat temp;
        if (!video.read_frame(temp))
        {
            std::cout << "视频播放结束..." << std::endl;
            break;
        }
        
        Frame frame;
        if (!frame.get_frame(temp))
        {
            std::cout << "读取帧失败..." << std::endl;
            continue;
        }
        if (!frame.color_swap())
        {
            std::cout << "颜色转换失败..." << std::endl;
            continue;
        }
        if (!frame.morph_denoise())
        {
            std::cout << "形态学去噪失败..." << std::endl;
            continue;
        }
        if (!frame.find_contours())
        {
            std::cout << "轮廓查找失败..." << std::endl;
            continue;
        }
        if (!frame.filter_contours())
        {
            std::cout << "轮廓匹配失败..." << std::endl;
            continue;
        }
        frame.pair_light();
        if (!frame.check_show())
        {
            std:: cout << "绘制失败..." << std::endl;
            continue;
        }
    }

    video.close();
    std::cout << "视频播放结束 ..." << std::endl;
    return 0;
}

