// Spike: assinante TwistStamped nativo no Android (handoff §10 passo 5).
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "rclcpp/rclcpp.hpp"

class TwistListener : public rclcpp::Node {
 public:
  TwistListener() : Node("android_twist_listener") {
    sub_ = create_subscription<geometry_msgs::msg::TwistStamped>(
        "cmd_vel_stamped", 10,
        [this](const geometry_msgs::msg::TwistStamped& m) {
          RCLCPP_INFO(get_logger(), "heard twist: frame=%s lin.x=%.2f ang.z=%.2f",
                      m.header.frame_id.c_str(), m.twist.linear.x,
                      m.twist.angular.z);
        });
  }

 private:
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr sub_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TwistListener>());
  rclcpp::shutdown();
  return 0;
}
