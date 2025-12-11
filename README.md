
-----

# 線上遊戲商城 - 使用者指南

這是一個整合了 **遊戲大廳 (Lobby)** 與 **遊戲商城 (Store)** 的網路程式系統。本專案分為 Server 端、Developer Client 端與 Player Client 端，支援遊戲上架、版本更新、自動下載、房間對戰以及評分系統。

## 📂 目錄結構

```
GameStore_System/
├── Makefile                 # 編譯腳本
├── server_app               # 編譯後的 Server 執行檔 (make後產生)
├── dev_app                  # 編譯後的 Developer Client 執行檔 (make後產生)
├── player_app               # 編譯後的 Player Client 執行檔 (make後產生)
├── basic.cpp / hpp          # 基礎網路工具模板
├── json.hpp                 # JSON 處理庫
├── server/                  # Server 端原始碼與資料庫
│   ├── main.cpp
│   ├── db.hpp
│   ├── room.hpp
│   └── uploaded_games/      # [自動生成] 存放已上架的遊戲檔案
├── client_dev/              # Developer 端
│   ├── developer.cpp
│   └── games/               # [自動生成] 開發者能存放未上架遊戲
├── client_player/           # Player 端
│   ├── player.cpp
│   └── downloads/           # [自動生成] 玩家下載的遊戲
└── templates/               # 遊戲範本
    └── game_template.py     # 標準 Python 遊戲腳本 (TicTacToe / Connect4)
```

## 快速啟動流程

### 1\. 環境需求

  * **C++ 編譯器**: 支援 C++17 (如 g++)
  * **Python**: 系統需安裝 `python3` 以執行遊戲腳本
  * **OS**: macOS (激推！)/ Linux(推) / Windows

### 2\. 編譯與重置

在專案根目錄執行以下指令，這會自動建立所需資料夾並編譯所有程式：

```bash
make clean
make
```

### 3\. 啟動順序

請開啟多個終端機視窗 (Terminal)，依序執行：

1.  **啟動 Server** (必須最先啟動):
    ```bash
    ./server_app
    ```
2.  **啟動 Developer Client** (進行上架/管理):
    ```bash
    ./dev_app
    ```
3.  **啟動 Player Client** (進行遊玩及評論):
    ```bash
    ./player_app
    ```

-----

## 開發者指南

開發者負責設計遊戲、測試並將其發布到商城。

### 流程 A: 開發與測試新遊戲

1.  **取得範本**: 將 `templates/game_template.py` 複製到 `client_dev/games/` 資料夾中，並重新命名 (例如 `my_game.py`)。
2.  **修改遊戲**: 編輯 `my_game.py`。你可以修改 `ACTIVE_GAME_CLASS` 變數來切換 **Tic-Tac-Toe** 或 **Connect Four**，或修改 `ConnectFourGame` 類別內的 `ROWS`, `COLS` 參數來改變棋盤大小。
3.  **本地測試**: 在上傳前，建議自行在本機手動執行 python 指令測試邏輯。

### 流程 B: 上架新遊戲

1.  啟動 `./dev_app` 並註冊/登入 Developer 帳號。
2.  選擇 **2. Upload New Game**。
3.  依照提示輸入資訊：
      * **Game Name**: 遊戲名稱 (不可與現有遊戲重名)。
      * **Version**: 版本號 (如 `1.0`)。
      * **Type**: 輸入 `CLI` 或 `GUI`。
      * **Max Players**: 輸入遊玩人數 (如 `2`)。
      * **Description**: 遊戲簡介。
      * **File Path**: 輸入你的本地檔案路徑 (例如 `client_dev/games/my_game.py`)。
4.  **注意**: 系統會檢查必填欄位，若留空會要求重輸；若檔案路徑錯誤會提示重試。

### 流程 C: 更新遊戲 

1.  選擇 **3. Update Existing Game**。
2.  系統會列出 **你擁有** 的遊戲。
3.  輸入要更新的遊戲編號。
4.  **智慧更新**: 針對不想修改的欄位 (如描述、類型)，直接按 **ENTER** 即可保留舊值。
5.  **版本檢查**: 新版本號必須大於舊版本 (例如 `1.0` -\> `1.1` 是合法的，`2.0` -\> `1.5` 會被拒絕)。

### 流程 D: 下架遊戲

1.  選擇 **4. Remove Game**。
2.  **安全機制**: 若該遊戲目前有 **正在進行中** 或 **等待中** 的房間，系統會拒絕下架，避免影響線上玩家。

-----

## 玩家指南

玩家可以瀏覽商城、下載遊戲、建立房間並進行對戰。

### 流程 A: 瀏覽商城與評分

1.  啟動 `./player_app` 並註冊/登入 Player 帳號。
2.  選擇 **1. Game Store**。
3.  列表會顯示遊戲名稱、版本狀態 (`[New]`, `[Installed]`, `[Update Available]`)、平均評分以及 **總下載人數(DL)**。
4.  輸入編號進入詳細頁面，可查看開發者資訊與其他玩家的評論。
5.  **評分 (Rate)**: 在詳細頁面選擇 **2. Rate this Game**。
      * **限制**: 必須 **實際遊玩過** 該遊戲才有資格評分。
      * **格式**: 僅接受 1-5 的正整數。

### 流程 B: 建立房間與遊玩

1.  在大廳選擇 **3. Create Room**。
2.  系統會列出可玩遊戲，輸入遊戲名稱。
3.  **自動下載**: 若本地無檔案或版本過舊，系統會自動從 Server 下載最新版。
4.  等待其他玩家加入。
5.  當房間人數達到該遊戲的指定 `Max Players` 後，房主輸入 **1** 即可開始遊戲。

### 流程 C: 加入房間

1.  在大廳選擇 **4. Join Room**。
2.  輸入 Room ID。
3.  系統會自動下載對應遊戲並進入房間。
4.  等待房主開始遊戲。

### 流程 D: 遊戲進行

1.  遊戲啟動後會彈出 Python視窗。
2.  **猜拳 (RPS)**: 雙方先進行猜拳決定先後手。平手會自動重來。
3.  **對戰**: 輪流輸入指令 (如 Tic-Tac-Toe 輸入 1-9，Connect Four 輸入 1-7)。
      * **防呆**: 若非你的回合，輸入無效；若輸入錯誤格式，系統會警告。
4.  **結算**: 遊戲結束後顯示勝負結果與總步數。按 Enter 關閉視窗回到大廳。

-----

---

## 預設測試資料

為了方便快速理解與驗收，系統資料庫 (`database.json`) 已預先載入以下帳號與遊戲資料。您可以直接使用這些帳號登入，無需重新註冊。

### 1. 預設帳號

| Role (身份) | Username (帳號) | Password (密碼) | 備註 |
| :--- | :--- | :--- | :--- |
| **Developer** | `dev_Erik` | `Erik1024` | 已上架一款 Tic-Tac-Toe 遊戲 |
| **Player** | `Erik` | `Erik1024` | 已下載並評分過 (5分) |
| **Player** | `David` | `David1234` | 已下載並評分過 (1分) |

### 2. 預設上架遊戲

* **Game Name**: `Tic-Tac-Toe`
* **Version**: `1.0`
* **Current Rating**: `3.0` (由 Erik 的 5 分與 David 的 1 分平均而來)
* **Downloads**: `2` (Erik 與 David)

---


## ⚠️ 注意事項

1.  **執行路徑**

      * 請務必在 **Project\_Root** (即包含 `Makefile` 的目錄) 執行所有程式 (`./server_app`, `./dev_app` 等)。
      * 切勿進入 `server/` 或 `client_dev/` 子目錄執行，否則會導致相對路徑錯誤 (File not found)。

2.  **遊戲名稱唯一性**

      * 開發者 **不能** 上傳一個已經被 **其他開發者** 註冊的遊戲名稱。
      * 若名稱衝突，Server 會拒絕請求。

3.  **下載次數計算**

      * 商城的 "Total Players Downloaded" 是計算 **不重複的玩家總數**。
      * 同一位玩家多次更新或下載同一款遊戲，計數不會增加。

4.  **輸入驗證**

      * 註冊帳號時，**不可使用空白鍵** 或全空白字串。
      * 遊戲評分時，**不可輸入小數** (如 4.5)，僅接受整數。

5.  **連接埠問題**

      * 若遊戲結束後房主想 **立刻** 再開一局，Python 腳本已設定 `SO_REUSEADDR` 以避免 `Address already in use` 錯誤。請確保開發者上傳的是最新版的 `game_template.py`。

6.  **房間 ID 機制**

      * 系統會自動尋找 **最小可用** 的 Room ID。例如房間 1, 2, 3 存在，若房間 2 解散，下一個建立的房間 ID 將會是 2。

-----
