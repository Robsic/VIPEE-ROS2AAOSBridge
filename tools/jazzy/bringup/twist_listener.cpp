// Spike: assinante TwistStamped nativo no Android (handoff §10 passo 5).
// Loga no logcat (tag vipee_twist): com rcl_logging_noop o RCLCPP_* só vai
// para stdout, que um serviço init não expõe.
#include <android/log.h>

#include <exception>

#include "geometry_msgs/msg/twist_stamped.hpp"
#include "rclcpp/rclcpp.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "vipee_twist", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "vipee_twist", __VA_ARGS__)

class TwistListener : public rclcpp::Node {
 public:
  TwistListener() : Node("android_twist_listener") {
    sub_ = create_subscription<geometry_msgs::msg::TwistStamped>(
        "cmd_vel_stamped", 10,
        [](const geometry_msgs::msg::TwistStamped& m) {
          LOGI("heard twist: frame=%s lin.x=%.2f ang.z=%.2f",
               m.header.frame_id.c_str(), m.twist.linear.x, m.twist.angular.z);
        });
  }

 private:
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr sub_;
};

int main(int argc, char** argv) {
  try {
    rclcpp::init(argc, argv);
    LOGI("rclcpp::init OK");
    rclcpp::spin(std::make_shared<TwistListener>());
    rclcpp::shutdown();
  } catch (const std::exception& e) {
    LOGE("fatal: %s", e.what());
    return 1;
  }
  return 0;
}
