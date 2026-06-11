// esp01s-miniapp/app.js
App({
  globalData: {
    ledState: {
      power: 1,
      brightness: 80,
      mode: "breathing",
      speed: 3
    },
    topicControl: "esp01s/led/control",
    topicStatus: "esp01s/led/status"
  }
});
