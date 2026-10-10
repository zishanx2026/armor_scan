#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <opencv2/opencv.hpp>

enum class EnemyColor {
    RED,
    BLUE
};

struct Armor
{
    cv::RotatedRect left;
    cv::RotatedRect right;
    cv::Point2f center;
    float width;
    float angle;
    EnemyColor color;
   
    int id;              // 唯一 ID
    int miss_count = 0;  // 连续丢失帧数
};
class Frame
{
public:
    Frame() = default;
    ~Frame() = default;
    
    
    bool get_frame(cv::Mat& img);
    bool color_swap();
    bool morph_denoise();
    bool find_contours();
    bool filter_contours();
    bool pair_light();
    bool check_show();

    std::vector<Armor> pair_bars(const std::vector<cv::RotatedRect>& bars, EnemyColor color);
    std::vector<cv::RotatedRect> filter_bars(
    const std::vector<std::vector<cv::Point>>& contours, EnemyColor color);
private:
    std::vector<Armor> m_armors_blue;
    std::vector<Armor> m_armors_red;
    std::vector<Armor> m_armors_all;

    // 缓存
    std::vector<cv::RotatedRect> m_prev_bars_blue;   
    std::vector<cv::RotatedRect> m_prev_bars_red;

    cv::Mat m_img_first;
    cv::Mat m_colorswap_blue;
    cv::Mat m_colorswap_red;
    std::vector<std::vector<cv::Point>> m_contours_blue;
    std::vector<std::vector<cv::Point>> m_contours_red;
    std::vector<cv::RotatedRect> m_light_bars_blue;
    std::vector<cv::RotatedRect> m_light_bars_red;

    std::vector<Armor> m_prev_armors;   // 上一帧的装甲板
    int m_miss_count = 0;                // 连续丢失帧数
     
    std::vector<Armor> m_tracked_armors;   // 跟踪列表
    int m_next_id = 0;                     // 分配 ID 用
    static constexpr int MAX_MISS = 20;    // 最多补 10 帧
    static constexpr float MATCH_DIST = 60; // 匹配距离阈值

};