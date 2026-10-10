#include "frame.h"
#include <vector>


bool Frame::get_frame(cv::Mat& img)
{
    if (img.empty()) return false; 
    m_img_first = img;
    return true;
}

bool Frame::color_swap()
{
    if (m_img_first.empty()) return false;

    std::vector<cv::Mat> channels;
    cv::split(m_img_first, channels);
    cv::Mat B = channels[0], R = channels[2];

    cv::Mat diff_blue, diff_red;
    cv::subtract(B, R, diff_blue);
    cv::subtract(R, B, diff_red);

    // 只用颜色特征，不用亮度
    cv::threshold(diff_blue, m_colorswap_blue, 80, 255, cv::THRESH_BINARY);
    cv::threshold(diff_red,  m_colorswap_red,  110, 255, cv::THRESH_BINARY);

    return true;
}
bool Frame::morph_denoise()
{
    if (m_colorswap_blue.empty() || m_colorswap_red.empty()) return false;

    cv::Mat kernel_red = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, 5));
    cv::Mat kernel_blue = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 5));
   
    cv::dilate(m_colorswap_red, m_colorswap_red, kernel_red);
    cv::morphologyEx(m_colorswap_blue, m_colorswap_blue, cv::MORPH_CLOSE, kernel_blue);
    cv::morphologyEx(m_colorswap_red, m_colorswap_red, cv::MORPH_CLOSE, kernel_red);
    return true;
}

bool Frame::find_contours()
{
    if (m_colorswap_blue.empty() || m_colorswap_red.empty()) return false;



    m_contours_red.clear();
    cv::findContours(m_colorswap_red, m_contours_red, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    m_contours_blue.clear();
    cv::findContours(m_colorswap_blue, m_contours_blue, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);    
    return true;
}
// 筛选灯
std::vector<cv::RotatedRect> Frame::filter_bars(
    const std::vector<std::vector<cv::Point>>& contours, EnemyColor color)
{
    std::vector<cv::RotatedRect> bars;

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        cv::RotatedRect rect = cv::minAreaRect(contour);
        float w = rect.size.width;
        float h = rect.size.height;
        if (w == 0 || h == 0) continue;

        float long_side  = std::max(w, h);
        float short_side = std::min(w, h);
        float ratio = long_side / short_side;
        float fill_ratio = area / (w * h);

        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        float hull_area = cv::contourArea(hull);
        float solidity = area / hull_area;

        // ★ 红蓝用不同的筛选参数 
        if (color == EnemyColor::RED)
        {
            // 红色
            if (fill_ratio < 0.6f) continue;
            if (area < 18 || area > 1040) continue;
            if (ratio < 1.0f || ratio > 6.0f) continue;
            if (short_side > 15) continue;
            if (long_side < 4 || long_side > 50) continue;
            if (solidity < 0.7) continue;
        } 
        else {
            // 蓝色
            if (fill_ratio < 0.4f) continue;           
            if (area < 8 || area > 900) continue;    
            if (ratio < 1.5f || ratio > 7.0f) continue; 
            if (short_side > 10) continue;              
            if (long_side < 4 || long_side > 45) continue; 
            if (solidity < 0.5) continue;
        }

        // 方向判断
        cv::Point2f pts[4];
        rect.points(pts);
        float len1 = cv::norm(pts[1] - pts[0]);
        float len2 = cv::norm(pts[2] - pts[1]);
        cv::Point2f dir;
        if (len1 > len2) dir = pts[1] - pts[0];
        else dir = pts[2] - pts[1];

        float angle_deg = std::atan2(std::abs(dir.y), std::abs(dir.x)) * 180.0f / CV_PI;
        if (angle_deg < 70.0f) continue;
        
        // ★ 蓝色凸性要求更高
        if (color == EnemyColor::BLUE) 
        {
            if (solidity < 0.7f) continue;
        } 
        else {
            if (solidity < 0.4f) continue;
        }

        bars.push_back(rect);
    }

    return bars;
}
bool Frame::filter_contours()
{
    m_light_bars_red  = filter_bars(m_contours_red,  EnemyColor::RED);
    m_light_bars_blue = filter_bars(m_contours_blue, EnemyColor::BLUE);
    
    // 如果当前帧蓝色灯条少于 2 个（说明漏检了），用上一帧的补上
    if (m_light_bars_blue.size() < 0 && !m_prev_bars_blue.empty()) 
    {
    // 把上一帧的灯条加入当前帧
        for (auto& prev : m_prev_bars_blue) 
        {
        m_light_bars_blue.push_back(prev);
        }
    }
// 更新缓存
    m_prev_bars_blue = m_light_bars_blue;
    return true;
}

std::vector<Armor> Frame::pair_bars(
    const std::vector<cv::RotatedRect>& bars, EnemyColor color)
{
    std::vector<Armor> armors_out;
    if (bars.size() < 2) return armors_out;

    // 按 x 坐标排序
    std::vector<cv::RotatedRect> sorted = bars;
    std::sort(sorted.begin(), sorted.end(),
        [](const cv::RotatedRect& a, const cv::RotatedRect& b) {
            return a.center.x < b.center.x;
        });

    // ★ 关键：used 标记数组
    std::vector<bool> used(sorted.size(), false);

    // ===== 两层循环，通过条件直接配对 =====
    for (size_t i = 0; i < sorted.size()-1; i++) 
    {// ★ 已使用的灯条跳过
        //if (used[i]) continue;   

        for (size_t j = i + 1; j < i+2; j++) {
            //if (used[j]) continue;   

            const auto& left_temp  = sorted[i];
            const auto& right_temp = sorted[j];

            float h_l = std::max(left_temp.size.width, left_temp.size.height);
            float h_r = std::max(right_temp.size.width, right_temp.size.height);
            if (h_l < 3 || h_r < 3) continue;

            float h_diff = std::abs(h_l - h_r);
            float h_diff_ratio = h_diff / std::max(h_l, h_r);
            if (h_diff_ratio > 0.3f) continue;

            float dist = cv::norm(left_temp.center - right_temp.center);
            if (dist < 15 || dist > 150) continue;

            float ratio_dist = dist / std::max(h_l, h_r);
            if (ratio_dist < 1.0f || ratio_dist > 6.0f) continue;

            float dx = left_temp.center.x - right_temp.center.x;
            float dy = left_temp.center.y - right_temp.center.y;
            float slope = std::abs(dy) / (std::abs(dx) + 1e-6f);
            if (slope > 0.5f) continue;

            float h_avg = (h_l + h_r) * 0.5f;
            if (std::abs(dy) > 0.5f * h_avg) continue;

            cv::Point2f pts_l[4], pts_r[4];
            left_temp.points(pts_l);
            right_temp.points(pts_r);
            cv::Point2f dir_l = pts_l[2] - pts_l[1];
            cv::Point2f dir_r = pts_r[2] - pts_r[1];
            float len_l = cv::norm(dir_l);
            float len_r = cv::norm(dir_r);
            if (len_l < 1e-6f || len_r < 1e-6f) continue;
            dir_l /= len_l;
            dir_r /= len_r;

            float dot = std::abs(dir_l.x * dir_r.x + dir_l.y * dir_r.y);
            if (dot < 0.8f) continue;

            // ===== 通过所有条件，组装 Armor =====
            Armor armor;
            armor.left  = left_temp;
            armor.right = right_temp;
            armor.center = (left_temp.center + right_temp.center) * 0.5f;
            armor.width = dist;
            armor.angle = std::atan2(
                right_temp.center.y - left_temp.center.y,
                right_temp.center.x - left_temp.center.x
            ) * 180.0f / CV_PI;
            armor.color = color;

            armors_out.push_back(armor);
            // ★ 跳出内层循环，`i` 已经被使用，处理下一个 `i`
            break;
        }
    }

    return armors_out;
}
bool Frame::pair_light()
{
    std::vector<Armor> measured;
    m_armors_red  = pair_bars(m_light_bars_red,  EnemyColor::RED);
    m_armors_blue = pair_bars(m_light_bars_blue, EnemyColor::BLUE);

    // 合并总列表
    measured.clear();
    for (const auto& a : m_armors_red)  measured.push_back(a);
    for (const auto& a : m_armors_blue) measured.push_back(a);
    if (measured.empty()) {
        // 当前帧没检出任何装甲板
        if (!m_prev_armors.empty() && m_miss_count < MAX_MISS) {
            // 用上一帧的结果顶上
            m_armors_all = m_prev_armors;
            m_miss_count++;
          
        }
    } else {
        // 当前帧检出了，更新缓存
        m_prev_armors = m_armors_all;
        m_miss_count = 0;
    }
    std::vector<bool> measured_used(measured.size(), false);

    for (auto& t : m_tracked_armors) {
        // 找最近的观测
        int best_idx = -1;
        float best_dist = 1e9;
        for (size_t i = 0; i < measured.size(); i++) {
            if (measured_used[i]) continue;
            float d = cv::norm(t.center - measured[i].center);
            if (d < best_dist) {
                best_dist = d;
                best_idx = i;
            }
        }

        if (best_idx >= 0 && best_dist < MATCH_DIST) {
            
            Armor& obs = measured[best_idx];
            t.left   = obs.left;
            t.right  = obs.right;
            t.center = obs.center;
            t.width  = obs.width;
            t.angle  = obs.angle;
            t.color  = obs.color;
            t.miss_count = 0;
            
            measured_used[best_idx] = true;
        } else {
            t.miss_count++;
        }
    }

    m_tracked_armors.erase(
        std::remove_if(m_tracked_armors.begin(), m_tracked_armors.end(),
            [](const Armor& a) { return a.miss_count > MAX_MISS; }),
        m_tracked_armors.end()
    );

    for (size_t i = 0; i < measured.size(); i++) {
        if (measured_used[i]) continue;
        Armor a = measured[i];
        a.id = m_next_id++;
        a.miss_count = 0;;
        m_tracked_armors.push_back(a);
    }

    m_armors_all = m_tracked_armors;

    return !m_armors_all.empty();
}

bool Frame::check_show()
{    
    cv::Mat out = m_img_first.clone();
    if (m_colorswap_blue.empty() || m_colorswap_red.empty()) return false;
    for (const auto& rect : m_light_bars_red) 
    {
        cv::Point2f pts[4];
        rect.points(pts);
        for (int i = 0; i < 4; i++) 
        {
            cv::line(out, pts[i], pts[(i + 1) % 4],
                     cv::Scalar(128, 128, 128), 1);   // 灰色
        }
    }
    for (const auto& rect : m_light_bars_blue) 
    {
        cv::Point2f pts[4];
        rect.points(pts);
        for (int i = 0; i < 4; i++) 
        {
            cv::line(out, pts[i], pts[(i + 1) % 4],
                     cv::Scalar(128, 128, 128), 1);   // 灰色
        }
    }
    // 画装甲板（绿）
    for (const auto& armor : m_armors_all) {
        float armor_h = std::max({armor.left.size.width, armor.left.size.height,
            armor.right.size.width, armor.right.size.height}
        );
        cv::RotatedRect armor_rect(armor.center, cv::Size2f(armor.width, armor_h), armor.angle);
        cv::Point2f pts[4];
        armor_rect.points(pts);
        for (int i = 0; i < 4; i++) 
        {
            cv::line(out, pts[i], pts[(i+1)%4], cv::Scalar(0, 255, 0), 2);
        }
        cv::circle(out, armor.center, 5, cv::Scalar(0, 255, 0), -1);
    }
    cv::imshow("Color Swapped Frame - blue", m_colorswap_blue);
    cv::imshow("Color Swapped Frame - Red", m_colorswap_red);
    cv::imshow("Color Swapped Frame - out", out);
    cv::waitKey(1);
    return true;
}