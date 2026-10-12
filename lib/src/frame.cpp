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

    // 颜色特征
    cv::Mat mask_blue_color, mask_red_color;
    cv::threshold(diff_blue, mask_blue_color, 50, 255, cv::THRESH_BINARY);
    cv::threshold(diff_red,  mask_red_color,  90, 255, cv::THRESH_BINARY);

    // ★ 亮度特征：过曝灯条（灰度 > 220）
    cv::Mat gray;
    cv::cvtColor(m_img_first, gray, cv::COLOR_BGR2GRAY);
    cv::Mat bright_mask;
    cv::threshold(gray, bright_mask, 200, 255, cv::THRESH_BINARY);

    // ★ 蓝色 = 颜色特征 OR (颜色 + 亮度) —— 只保蓝色区域的过曝
    cv::Mat mask_blue_bright;
    cv::bitwise_and(mask_blue_color, bright_mask, mask_blue_bright);
    cv::bitwise_or(mask_blue_color, mask_blue_bright, m_colorswap_blue);

    // ★ 红色同理
    cv::Mat mask_red_bright;
    cv::bitwise_and(mask_red_color, bright_mask, mask_red_bright);
    cv::bitwise_or(mask_red_color, mask_red_bright, m_colorswap_red);

    return true;
}
bool Frame::morph_denoise()
{
    if (m_colorswap_blue.empty() || m_colorswap_red.empty()) return false;

    cv::Mat kernel_red = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 5));
    cv::dilate(m_colorswap_red, m_colorswap_red, kernel_red);
    cv::morphologyEx(m_colorswap_red, m_colorswap_red, cv::MORPH_CLOSE, kernel_red);

    // ★ 蓝色
    cv::Mat kernel_blue = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, 7));
    cv::morphologyEx(m_colorswap_blue, m_colorswap_blue, cv::MORPH_CLOSE, kernel_blue);

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
            if (fill_ratio < 0.3f) continue;
            if (area < 30 || area > 1040) continue;
            if (ratio < 1.0f || ratio > 8.0f) continue;
            if (short_side > 15) continue;
            if (long_side < 1 || long_side > 40) continue;
            if (solidity < 0.55) continue;
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
        } 
        else {
            // 蓝色
            if (fill_ratio < 0.45f) continue;           
            if (area < 15 || area > 1000) continue;    
            if (ratio < 1.0f || ratio > 7.0f) continue; 
            if (short_side > 20) continue;      
            if (long_side < 2 || long_side > 40) continue; 
            if (solidity < 0.4) continue;

            // 方向判断
            cv::Point2f pts[4];
            rect.points(pts);
            float len1 = cv::norm(pts[1] - pts[0]);
            float len2 = cv::norm(pts[2] - pts[1]);
            cv::Point2f dir;
            if (len1 > len2) dir = pts[1] - pts[0];
            else dir = pts[2] - pts[1];

            float angle_deg = std::atan2(std::abs(dir.y), std::abs(dir.x)) * 180.0f / CV_PI;
            if (angle_deg < 65.0f) continue;
            /*if (fill_ratio < 0.3f) continue;           
            if (area < 8 || area > 900) continue;    
            if (ratio < 1.5f || ratio > 10.0f) continue; 
            if (short_side > 10) continue;              
            if (long_side < 4 || long_side > 45) continue; 
            if (solidity < 0.4) continue;*/
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

    // ===== 1. 收集所有候选 =====
    struct Candidate {
        size_t i, j;
        float score;   // 越小越优
        Armor armor;
    };
    std::vector<Candidate> candidates;

    for (size_t i = 0; i < sorted.size(); i++) {
        for (size_t j = i + 1; j < sorted.size(); j++) {

            const auto& left_temp  = sorted[i];
            const auto& right_temp = sorted[j];

            // 高度过滤
            float h_l = std::max(left_temp.size.width, left_temp.size.height);
            float h_r = std::max(right_temp.size.width, right_temp.size.height);
            if (h_l < 2 || h_r < 2) continue;

             // 中间灯条检查（跳过已标记的灯条）
            bool bar_between = false;
            for (size_t k = i + 1; k < j; k++) {
                float h_mid = std::max(sorted[k].size.width, sorted[k].size.height);
                if (sorted[k].center.x > sorted[i].center.x &&
                    sorted[k].center.x < sorted[j].center.x &&
                    h_mid > 0.7f * std::min(h_l, h_r)) 
                {   
                    bar_between = true;
                    break;
                }
            }
            if (bar_between) continue;

            float h_diff = 0;
            float h_diff_ratio = 0; 
            // 距离过滤
            float dist = cv::norm(left_temp.center - right_temp.center);
            // 间距/灯条长度比例
            float ratio_dist = 0;
            // 连线倾斜度
            float slope = 0;
            // y 坐标差
            float h_avg = 0;
            float dot = 0;
            if (color == EnemyColor::RED)
            {
                h_diff = std::abs(h_l - h_r);
                h_diff_ratio = h_diff / std::max(h_l, h_r);
                // 稍微放宽（0.6 -> 0.7）
                if (h_diff_ratio > 0.7f) continue;

                // 距离下限放宽（10 -> 5），防止近距离灯条配不上
                /*if (dist < 10 || dist > 200) continue;

                ratio_dist = dist / std::max(h_l, h_r);
                if (ratio_dist < 0.8f || ratio_dist > 10.0f) continue;

                float dx = left_temp.center.x - right_temp.center.x;
                float dy = left_temp.center.y - right_temp.center.y;
                slope = std::abs(dy) / (std::abs(dx) + 1e-6f);
                // 放宽倾斜度
                if (slope > 0.7f) continue;

                h_avg = (h_l + h_r) * 0.5f;
                // ★ 最关键：放宽中心高度差限制（0.5 -> 0.9），给破碎灯条容错空间
                if (std::abs(dy) > 0.9f * h_avg) continue;

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

                dot = std::abs(dir_l.x * dir_r.x + dir_l.y * dir_r.y);
                // 放宽方向一致性（0.8 -> 0.7），容忍破碎带来的方向偏差
                if (dot < 0.6f) continue;*/
                h_diff = std::abs(h_l - h_r);
                h_diff_ratio = h_diff / std::max(h_l, h_r);
                // 稍微放宽（0.6 -> 0.7）
                if (h_diff_ratio > 0.7f) continue;

                // 距离下限放宽（10 -> 5），防止近距离灯条配不上
                if (dist < 5 || dist > 200) continue;

                ratio_dist = dist / std::max(h_l, h_r);
                if (ratio_dist < 0.5f || ratio_dist > 8.0f) continue;

                float dx = left_temp.center.x - right_temp.center.x;
                float dy = left_temp.center.y - right_temp.center.y;
                slope = std::abs(dy) / (std::abs(dx) + 1e-6f);
                // 放宽倾斜度
                if (slope > 0.85f) continue;

                h_avg = (h_l + h_r) * 0.5f;
                // ★ 最关键：放宽中心高度差限制（0.5 -> 0.9），给破碎灯条容错空间
                if (std::abs(dy) > 0.9f * h_avg) continue;

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

                dot = std::abs(dir_l.x * dir_r.x + dir_l.y * dir_r.y);
                // 放宽方向一致性（0.8 -> 0.7），容忍破碎带来的方向偏差
                if (dot < 0.6f) continue;
            }
            else{  // 蓝色的标准
                h_diff = std::abs(h_l - h_r);
                h_diff_ratio = h_diff / std::max(h_l, h_r);
                if (h_diff_ratio > 0.8f) continue;
                if (dist < 10 || dist > 90) continue;
                if (h_r < h_l*0.55f || h_r*0.55f  > h_l) continue;

                ratio_dist = dist / std::max(h_l, h_r);
                if (ratio_dist < 0.5f || ratio_dist > 6.0f) continue;

                float dx = left_temp.center.x - right_temp.center.x;
                float dy = left_temp.center.y - right_temp.center.y;
                slope = std::abs(dy) / (std::abs(dx) + 1e-6f);
                if (slope > 0.3f) continue;

                h_avg = (h_l + h_r) * 0.5f;
                //if (std::abs(dy) > 0.8f * h_avg) continue;

                /*cv::Point2f pts_l[4], pts_r[4];
                left_temp.points(pts_l);
                right_temp.points(pts_r);
                cv::Point2f dir_l = pts_l[2] - pts_l[1];
                cv::Point2f dir_r = pts_r[2] - pts_r[1];
                float len_l = cv::norm(dir_l);
                float len_r = cv::norm(dir_r);
                if (len_l < 1e-6f || len_r < 1e-6f) continue; 
                dir_l /= len_l;
                dir_r /= len_r;

                dot = std::abs(dir_l.x * dir_r.x + dir_l.y * dir_r.y);               
                if (dot < 0.7f) continue;
                /*h_diff = std::abs(h_l - h_r);
                h_diff_ratio = h_diff / std::max(h_l, h_r);
                if (h_diff_ratio > 0.6f) continue;
                if (dist < 10 || dist > 130) continue;

                ratio_dist = dist / std::max(h_l, h_r);
                if (ratio_dist < 0.8f || ratio_dist > 7.0f) continue;

                float dx = left_temp.center.x - right_temp.center.x;
                float dy = left_temp.center.y - right_temp.center.y;
                slope = std::abs(dy) / (std::abs(dx) + 1e-6f);
                if (slope > 0.75f) continue;

                h_avg = (h_l + h_r) * 0.5f;
                if (std::abs(dy) > 0.7f * h_avg) continue;

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

                dot = std::abs(dir_l.x * dir_r.x + dir_l.y * dir_r.y);               
                if (dot < 0.7f) continue;*/
            }
            // ===== 2. 打分 =====
            // score 越小越优，优先选“最像装甲板”的配对
            float score = 0;
            score += h_diff_ratio * 200.0f;                       // 高度差
            score += (1.0f - dot) * 150.0f;                        // 方向差
            score += slope * 30.0f;                                // 倾斜度

            // 比例偏差：越小越好（1.12 小装甲，1.85 大装甲）
            float ratio_err = std::min(
                std::abs(ratio_dist - 1.12f),
                std::abs(ratio_dist - 1.85f)
            );
            score += ratio_err * 20.0f;

            // 组装 Armor
            Armor armor;
            if (left_temp.center.x < right_temp.center.x) {
                armor.left  = left_temp;
                armor.right = right_temp;
            } else {
                armor.left  = right_temp;
                armor.right = left_temp;
            }
            armor.center = (left_temp.center + right_temp.center) * 0.5f;
            armor.width = dist;
            armor.angle = std::atan2(
                right_temp.center.y - left_temp.center.y,
                right_temp.center.x - left_temp.center.x
            ) * 180.0f / CV_PI;
            armor.color = color;

            Candidate node;
            node.i = i;
            node.j = j;
            node.score = score;
            node.armor = armor;
            candidates.push_back(node);
        }
    }

    // ===== 3. 按 score 排序（越小越优） =====
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.score < b.score;
        });

    // ===== 4. 贪心选择：每个灯条只用一次 =====
    std::vector<bool> used(sorted.size(), false);
    for (const auto& c : candidates) {
        if (used[c.i] || used[c.j]) continue;
        used[c.i] = true;
        used[c.j] = true;
        armors_out.push_back(c.armor);
    }

    return armors_out;
}
bool Frame::pair_light()
{
    // ===== 1. 当前帧观测 =====
    std::vector<Armor> measured;
    m_armors_red  = pair_bars(m_light_bars_red,  EnemyColor::RED);
    m_armors_blue = pair_bars(m_light_bars_blue, EnemyColor::BLUE);
    for (const auto& a : m_armors_red)  measured.push_back(a);
    for (const auto& a : m_armors_blue) measured.push_back(a);

    std::vector<bool> measured_used(measured.size(), false);

    // ===== 2. 匹配已有跟踪 =====
    for (auto& t : m_tracked_armors) {
        int best_idx = -1;
        float best_dist = 1e9;
        for (size_t i = 0; i < measured.size(); i++) {
            if (measured_used[i]) continue;
            float d = cv::norm(t.center - measured[i].center);
            if (d < best_dist) { best_dist = d; best_idx = i; }
        }

        if (best_idx >= 0 && best_dist < MATCH_DIST) {
            const auto& obs = measured[best_idx];
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

    // ===== 3. 移除过期跟踪 =====
    m_tracked_armors.erase(
        std::remove_if(m_tracked_armors.begin(), m_tracked_armors.end(),
            [](const Armor& a) { return a.miss_count > MAX_MISS; }),
        m_tracked_armors.end()
    );

    // ===== 4. 新增未匹配的观测 =====
    for (size_t i = 0; i < measured.size(); i++) {
        if (measured_used[i]) continue;
        Armor a = measured[i];
        a.id = m_next_id++;
        a.miss_count = 0;
        m_tracked_armors.push_back(a);
    }

    // ===== 5. 更新 m_armors_all =====
    m_armors_all = m_tracked_armors;

    // ===== 6. 补帧 =====
    if (!m_armors_all.empty()) {
        // 当前帧有结果 → 更新缓存
        m_prev_armors = m_armors_all;
        m_miss_count = 0;
    } else {
        // 当前帧没结果 → 补帧
        m_miss_count++;
        if (!m_prev_armors.empty() && m_miss_count < MAX_MISS) {
            m_armors_all = m_prev_armors;
        }
    }

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
                     cv::Scalar(0, 0, 255), 1);   // 灰色
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