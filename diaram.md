graph TD    %% Initialization    subgraph SETUP ["setup() : 系統初始化"]        S1(Serial.begin) --> S2(I2C 硬體初始化)        S2 --> S3(RTC 初始化與時間檢測)        S3 --> S4(LCD 初始化)        S4 --> S5(首次讀取 DHT)    en
    %% Main Loop
    subgraph LOOP ["loop() : 主迴圈 (非阻塞)"]
        L1{距離上次讀取 RTC\n大於 250ms?}
        L1 -- 否 --> L1_END(結束本次迴圈)
        L1 -- 是 --> L2(透過 I2C 讀取 RTC 時間)
        L2 --> L3{RTC 狀態 OK?}
        
        %% 正常模式 (RTC 驅動)
        L3 -- 是 --> L4{目前秒數 != 上次秒數?}
        L4 -- 否 (同秒) --> L1_END
        L4 -- 是 (跨秒) --> L5[更新輸出: Serial + LCD]
        L5 --> L6{距離上次讀取 DHT\n經過 5 秒?}
        L6 -- 是 --> L7[讀取 DHT 溫濕度]
        L6 -- 否 --> L1_END
        L7 --> L1_END
        %% 備援模式 (millis 驅動)
        L3 -- 否 --> F1{距離上次更新\n超過 1000ms?}
        F1 -- 否 --> L1_END
        F1 -- 是 --> F2[更新輸出: 顯示錯誤狀態]
        F2 --> F3{距離上次讀取 DHT\n經過 5000ms?}
        F3 -- 是 --> F4[讀取 DHT 溫濕度]
        F3 -- 否 --> L1_END
        F4 --> L1_END
    end
    SETUP --> LOOP
    L1_END -. 迴圈重試 .-> L1
    style SETUP fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    style LOOP fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    style L3 fill:#fff9c4,stroke:#fbc02d,stroke-width:2px