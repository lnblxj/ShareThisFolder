# STUN NAT 穿透隧道实现原理

## 概述

通过 STUN（Session Traversal Utilities for NAT）协议实现 NAT 穿透，将内网 HTTP 文件共享服务暴露到公网，使任意设备可通过公网 IP + 端口直接访问。

**前提条件**：NAT1（全锥形 NAT）环境，即运营商级 NAT 或路由器配置了 DMZ/UPnP。

## 核心原理

NAT 路由器在转发出站流量时，会创建一条映射规则：

```
内网IP:内网端口 → NAT分配 → 公网IP:公网端口
```

对于 NAT1，**任何外部主机**都可以通过 `公网IP:公网端口` 向内网主机发包，NAT 会自动转发到对应的 `内网IP:内网端口`。

STUN 穿透的关键是：
1. **主动创建 NAT 映射**（通过出站 TCP 连接）
2. **发现映射的公网地址**（通过 STUN 协议）
3. **保持映射存活**（通过周期性保活包）
4. **在映射端口上监听并转发**（将外部流量导向本地 HTTP 服务器）

## 三步实现流程

### Step 1: TCP 保活连接（创建 NAT 映射）

从本地端口 X 建立一条 TCP 出站连接到外部 HTTP 服务器：

```
本地 0.0.0.0:X ──TCP连接──> www.baidu.com:80
```

NAT 路由器看到出站流量后，创建映射：

```
公网IP:Y ←→ 内网IP:X
```

其中 Y 是 NAT 分配的公网端口（可能与 X 相同，也可能不同）。

**natmap 对应代码**：`hev-tnsk.c` → `hev_sock_client_base()` 创建 TCP 连接

### Step 2: STUN 协议发现公网地址

从**同一端口 X** 向 STUN 服务器发送 Binding Request，STUN 服务器返回你的公网地址：

```
本地 0.0.0.0:X ──STUN请求──> stun.nextcloud.com:3478
                                    ↓
                              返回 公网IP:Y
```

STUN 协议（RFC 5389）工作流程：
1. 客户端发送 20 字节 Binding Request（含 Magic Cookie 0x2112A442）
2. 服务器返回 Binding Response，包含 XOR-MAPPED-ADDRESS 属性
3. 该属性编码了客户端的公网 IP 和端口（XOR 混淆，需用 Magic Cookie 还原）

**XOR-MAPPED-ADDRESS 解析**：
```
端口 = 响应中的端口字段 XOR Magic Cookie 高16位
IP   = 响应中的地址字段 XOR Magic Cookie（逐字节XOR，保持网络字节序）
```

**natmap 对应代码**：`hev-stun.c` → `stun_pack()` / `stun_unpack()`

### Step 3: 转发服务器（端口映射）

在隧道端口 X 上启动一个 TCP 转发服务器，将外部连接转发到本地 HTTP 服务器：

```
外部浏览器 → 公网IP:Y → NAT → 内网IP:X → 转发服务器 → localhost:8888 → HTTP服务器
```

**Windows 端口共存**：
- 保活 socket：绑定到 `0.0.0.0:X`（出站连接，占用端口）
- 转发服务器：绑定到 `本地IP:X`（如 `192.168.0.109:X`）
- 通过 `SO_REUSEADDR` 实现同一端口上的两个 socket 共存
- 新入站连接会被路由到监听中的转发服务器（而非已连接的保活 socket）

**natmap 对应代码**：`hev-tfwd.c` → `hev_sock_server_pfwd()` 创建监听 + `hev_task_io_splice()` 双向转发

### 保活机制

NAT 映射有超时机制（通常 30-120 秒），需要定期发送数据维持映射：

```
每15秒: 保活socket → 发送 HTTP HEAD 请求 → 服务器响应 → 映射续期
```

natmap 使用 HTTP HEAD 请求作为保活包，同时维持 TCP 连接的活跃状态。

**natmap 对应代码**：`hev-tnsk.c` → `tnsk_keep_alive()`

## 与传统方案的对比

| 方案 | 是否需要公网服务器 | 是否直连 | 延迟 |
|------|-------------------|----------|------|
| STUN 穿透（本方案） | 否（使用公共 STUN） | 是 | 低 |
| FRP/内网穿透 | 是（中继服务器） | 否（经中继） | 中 |
| ngrok | 是（ngrok 服务器） | 否（经中继） | 中 |
| VPN | 是（VPN 服务器） | 否（经隧道） | 中高 |

## 限制条件

1. **NAT 类型**：仅 NAT1（全锥形）可靠工作。NAT2/NAT3 可能因端口限制或对称 NAT 导致失败
2. **端口随机性**：NAT 分配的公网端口不固定，可能随时变化
3. **ISP 限制**：部分运营商可能封锁 STUN 端口或过滤非标准流量
4. **Windows 端口共存**：必须使用具体本地 IP（非 0.0.0.0）绑定转发服务器

## 参考实现

- [natmap](https://github.com/heiher/natmap) - TCP/UDP port mapping for full-cone NAT
- [Lucky](https://www.lucky666.cn/docs/modules/stun) - STUN 内网穿透模块
- [RFC 5389](https://tools.ietf.org/html/rfc5389) - STUN 协议规范
