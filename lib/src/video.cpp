#include "video.h"
#include <iostream>


bool Video::check_open()
{
    if (!m_cap.isOpened()) return false;
    return true;
}

bool Video::check_empty()
{
    if (m_frame.empty()) return false;
    return true;
}

Video::Video(const std::string& path) : m_is_open(false) 
{
    m_cap.open(path);
    if (!m_cap.isOpened()) {
        std::cerr << "Video 打开失败: " << path << std::endl;
        return;
    }
    m_is_open = true;
    std::cout << "Video 打开成功: " << path << std::endl;
}
Video::~Video()
{
    close();
}

bool Video::is_opened()
{
    return m_is_open;
}

bool Video::read_frame(cv::Mat& out)
{
    return m_cap.read(out);
}

void Video::close()
{
    if (!m_cap.isOpened()) return;
    m_cap.release();
    cv::destroyAllWindows();
}