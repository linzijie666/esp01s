// esp01s-miniapp/app.js
const mqtt = require('./utils/mqtt.min.js');

// 兼容不同导出方式
const mqttConnect = mqtt.default && mqtt.default.connect ? mqtt.default.connect : (mqtt.connect || mqtt);

App({

  globalData: {
    // 设备状态
    ledState: {
      power: 1,
      brightness: 80,
      mode: "breathing",
      speed: 3
    },

    // MQTT Topic
    topicControl: "esp01s/led/control",
    topicStatus: "esp01s/led/status",

    // ── 全局 MQTT 单例 ──
    mqttConnected: false,
    mqttClient: null,

    // 消息回调
    _messageCallbacks: {}
  },

  /**
   * 确保 MQTT 已连接（幂等，全局只有 1 个 socket）
   */
  ensureMqttConnected() {
    const g = this.globalData;

    // 已连接
    if (g.mqttClient && g.mqttClient.connected) {
      return;
    }

    // 正在连接中
    if (g.mqttClient && g.mqttClient.reconnecting) {
      return;
    }

    console.log('[MQTT] 创建连接 → broker.emqx.io:8084/mqtt');

    const client = mqttConnect({
      protocol: 'wxs',
      host: 'broker.emqx.io',
      port: 8084,
      path: '/mqtt',
      clientId: 'wx_esp01s_' + Math.random().toString(16).substring(2, 10),
      keepalive: 60,
      reconnectPeriod: 5000,
      connectTimeout: 10000,
      clean: true,
      resubscribe: true
    });

    g.mqttClient = client;

    client.on('connect', () => {
      console.log('[MQTT] ✓ 已连接');
      g.mqttConnected = true;

      // 订阅状态主题
      client.subscribe(g.topicStatus, { qos: 1 }, (err) => {
        if (!err) {
          console.log('[MQTT] 已订阅', g.topicStatus);
        }
      });

      // 通知页面
      this._notifyCallbacks('__connect__', { connected: true });

      // 请求一次状态
      client.publish(g.topicControl, JSON.stringify({ cmd: 'status' }), { qos: 1 });
    });

    client.on('message', (topic, payload) => {
      this._notifyCallbacks(topic, payload);
    });

    client.on('error', (err) => {
      console.error('[MQTT] 错误:', err.message);
      g.mqttConnected = false;
      this._notifyCallbacks('__connect__', { connected: false });
    });

    client.on('close', () => {
      console.log('[MQTT] 连接关闭');
      g.mqttConnected = false;
      this._notifyCallbacks('__connect__', { connected: false });
    });

    client.on('reconnect', () => {
      console.log('[MQTT] 重连中...');
    });

    client.on('offline', () => {
      g.mqttConnected = false;
      this._notifyCallbacks('__connect__', { connected: false });
    });
  },

  // ── 消息回调 ──

  registerMessageCallback(topic, callback) {
    const cbs = this.globalData._messageCallbacks;
    if (!cbs[topic]) cbs[topic] = [];
    if (cbs[topic].indexOf(callback) === -1) {
      cbs[topic].push(callback);
    }
  },

  unregisterMessageCallback(topic, callback) {
    const cbs = this.globalData._messageCallbacks;
    if (!cbs[topic]) return;
    const idx = cbs[topic].indexOf(callback);
    if (idx !== -1) cbs[topic].splice(idx, 1);
  },

  _notifyCallbacks(topic, payload) {
    const cbs = this.globalData._messageCallbacks[topic];
    if (!cbs || cbs.length === 0) return;
    [...cbs].forEach(cb => {
      try { cb(payload); } catch (e) { console.error('[MQTT] 回调异常:', e); }
    });
  },

  // ── 断开连接 ──

  disconnectMqtt() {
    const g = this.globalData;
    if (g.mqttClient) {
      try { g.mqttClient.end(true); } catch (e) {}
      g.mqttClient = null;
      g.mqttConnected = false;
      g._messageCallbacks = {};
    }
  }
});
