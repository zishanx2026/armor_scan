#pragma once
#include <string>
#include <opencv2/opencv.hpp>

class Video
{
public:
    explicit Video(const std::string& path);
    ~Video();

    bool is_opened();
    bool read_frame(cv::Mat& out);
    void close();

private:
    bool check_open();
    bool check_empty();
    bool m_is_open; 
    cv::VideoCapture m_cap;
    cv::Mat m_frame;
};