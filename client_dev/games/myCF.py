import socket
import argparse
import threading
import json
import os
import time
import sys

# ==============================================================================
#  遊戲設定區
# ==============================================================================

# ACTIVE_GAME_CLASS = "TicTacToeGame" 
ACTIVE_GAME_CLASS = "ConnectFourGame"

# ==============================================================================
#  跨平台輸入緩衝清除工具
# ==============================================================================
def flush_input():
    """清除 stdin 緩衝區，防止非回合時的輸入殘留"""
    try:
        # Windows
        import msvcrt
        while msvcrt.kbhit():
            msvcrt.getch()
    except ImportError:
        # Linux / macOS
        import termios
        try:
            termios.tcflush(sys.stdin, termios.TCIOFLUSH)
        except:
            pass

# ==============================================================================
#  基礎遊戲框架
# ==============================================================================

class BaseGame:
    def __init__(self):
        self.game_over = False
        self.winner = None
        self.turn = None  
        self.history_count = 0 

    def init_game(self):
        pass

    def get_state(self):
        return {}

    def is_valid_move(self, move_input):
        return False

    def apply_move(self, move_input, player_role):
        pass

    def check_winner(self):
        pass

    def render(self, my_role):
        pass

    def get_input_prompt(self):
        return "Your move: "

    def validate_client_input(self, user_input):
        return True, ""

# ==============================================================================
#  遊戲 1: 圈圈叉叉 - 1-9 輸入版
# ==============================================================================
class TicTacToeGame(BaseGame):
    def __init__(self):
        super().__init__()
        self.board = [" "] * 9
        self.history = []
    
    def get_state(self):
        return {
            "board": self.board,
            "turn": self.turn,
            "winner": self.winner,
            "game_over": self.game_over,
            "history_count": len(self.history)
        }

    def is_valid_move(self, move_input):
        try:
            idx = int(move_input) - 1
            return 0 <= idx <= 8 and self.board[idx] == " "
        except:
            return False

    def validate_client_input(self, user_input):
        if not user_input.isdigit(): return False, "Input must be a number."
        val = int(user_input)
        if val < 1 or val > 9: return False, "Input must be 1-9."
        idx = val - 1
        if self.board[idx] != " ": return False, "That cell is already taken!"
        return True, ""

    def apply_move(self, move_input, player_role):
        idx = int(move_input) - 1
        self.board[idx] = player_role
        self.history.append(f"{player_role}->{move_input}")
        self.check_winner()
        if not self.game_over:
            self.turn = "O" if self.turn == "X" else "X"

    def check_winner(self):
        b = self.board
        wins = [(0,1,2),(3,4,5),(6,7,8),(0,3,6),(1,4,7),(2,5,8),(0,4,8),(2,4,6)]
        for x,y,z in wins:
            if b[x] == b[y] == b[z] and b[x] != " ":
                self.winner = b[x]
                self.game_over = True
                return
        if " " not in b: self.game_over = True

    def render(self, my_role):
        os.system('cls' if os.name == 'nt' else 'clear')
        b = self.board
        print(f"=== Tic-Tac-Toe (1-9) ===")
        print(f"You: {my_role} | Turn: {self.turn}\n")
        print(f" {b[0]} | {b[1]} | {b[2]} ")
        print("---+---+---")
        print(f" {b[3]} | {b[4]} | {b[5]} ")
        print("---+---+---")
        print(f" {b[6]} | {b[7]} | {b[8]} ")
        
        if not self.game_over:
            if self.turn == my_role:
                print(f"\n[YOUR TURN] Input 1-9 to place {my_role}:")
            else:
                print(f"\n[WAITING] Waiting for opponent...")

# ==============================================================================
#  遊戲 2: 四子棋 
# ==============================================================================
class ConnectFourGame(BaseGame):
    def __init__(self):
        super().__init__()
        self.ROWS = 6
        self.COLS = 7
        self.CONNECT_N = 4 
        self.board = [[" " for _ in range(self.COLS)] for _ in range(self.ROWS)]
        self.history = []

    def get_state(self):
        return {
            "board": self.board,
            "turn": self.turn,
            "winner": self.winner,
            "game_over": self.game_over,
            "history_count": len(self.history),
            "rows": self.ROWS,
            "cols": self.COLS
        }

    def is_valid_move(self, move_input):
        try:
            col = int(move_input) - 1
            if col < 0 or col >= self.COLS: return False
            return self.board[0][col] == " "
        except:
            return False

    def validate_client_input(self, user_input):
        if not user_input.isdigit(): return False, "Input must be a number."
        val = int(user_input)
        if val < 1 or val > self.COLS: return False, f"Input must be 1-{self.COLS}."
        
        col_idx = val - 1
        if self.board[0][col_idx] != " ":
            return False, "Column is full! Please choose another one."
        return True, ""

    def apply_move(self, move_input, player_role):
        col = int(move_input) - 1
        for r in range(self.ROWS - 1, -1, -1):
            if self.board[r][col] == " ":
                self.board[r][col] = player_role
                self.history.append(f"{player_role}->C{move_input}")
                self.check_winner_from(r, col, player_role)
                if not self.game_over:
                    self.turn = "O" if self.turn == "X" else "X"
                return True
        return False

    def check_winner_from(self, r, c, p):
        directions = [(0,1), (1,0), (1,1), (1,-1)]
        for dr, dc in directions:
            count = 1
            for k in range(1, self.CONNECT_N):
                nr, nc = r + dr*k, c + dc*k
                if 0 <= nr < self.ROWS and 0 <= nc < self.COLS and self.board[nr][nc] == p: count += 1
                else: break
            for k in range(1, self.CONNECT_N):
                nr, nc = r - dr*k, c - dc*k
                if 0 <= nr < self.ROWS and 0 <= nc < self.COLS and self.board[nr][nc] == p: count += 1
                else: break
            if count >= self.CONNECT_N:
                self.winner = p
                self.game_over = True
                return
        
        is_full = True
        for row in self.board:
            if " " in row: is_full = False
        if is_full: self.game_over = True

    def render(self, my_role):
        os.system('cls' if os.name == 'nt' else 'clear')
        print(f"=== Connect Four (Match {self.CONNECT_N}) ===")
        print(f"You: {my_role} | Turn: {self.turn}\n")
        
        for r in range(self.ROWS):
            row_str = "|"
            for c in range(self.COLS):
                row_str += f"{self.board[r][c]}|"
            print(row_str)
        print("-" * (self.COLS * 2 + 1))
        
        num_str = " "
        for c in range(1, self.COLS + 1): num_str += f"{c} "
        print(num_str)

        if not self.game_over:
            if self.turn == my_role:
                print(f"\n[YOUR TURN] Input 1-{self.COLS} to drop token:")
            else:
                print(f"\n[WAITING] Waiting for opponent...")

# ==============================================================================
#  結算與網路
# ==============================================================================

def show_checkout_screen(game_obj, my_role):
    os.system('cls' if os.name == 'nt' else 'clear')
    print("========================================")
    print("           GAME OVER - CHECKOUT         ")
    print("========================================")
    
    winner = game_obj.winner
    print(f"Winner: {winner if winner else 'Draw'}")
    
    if winner == my_role:
        print("\033[92mResult: YOU WIN! Congratulations!\033[0m")
    elif winner is None:
        print("\033[93mResult: Draw Game.\033[0m")
    else:
        print("\033[91mResult: YOU LOSE. Better luck next time.\033[0m")
        
    print("----------------------------------------")
    print(f"Total Moves: {game_obj.history_count}")
    print("========================================")
    flush_input()
    input("Press Enter to exit...")

def run_server(port):
    game_class = globals()[ACTIVE_GAME_CLASS]
    game = game_class()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', port))
    server.listen(2)
    print(f"[Server] Listening on {port}...")

    clients = []
    lock = threading.Lock()

    while len(clients) < 2:
        conn, addr = server.accept()
        clients.append(conn)
        print(f"Client {len(clients)} connected.")

    print("Starting RPS...")
    rps_winner = -1
    while rps_winner == -1:
        for c in clients: c.send((json.dumps({"type": "rps_req"}) + "\n").encode())
        moves = []
        for c in clients: moves.append(json.loads(c.recv(1024).decode().strip())["move"])
        
        m1, m2 = moves[0], moves[1]
        if m1 != m2:
            p1_wins = (m1 == 'R' and m2 == 'S') or (m1 == 'S' and m2 == 'P') or (m1 == 'P' and m2 == 'R')
            rps_winner = 0 if p1_wins else 1
            clients[0].send((json.dumps({"type": "rps_result", "res": "win" if p1_wins else "lose", "role": "X" if p1_wins else "O"}) + "\n").encode())
            clients[1].send((json.dumps({"type": "rps_result", "res": "lose" if p1_wins else "win", "role": "O" if p1_wins else "X"}) + "\n").encode())
        else:
            for c in clients: c.send((json.dumps({"type": "rps_result", "res": "draw"}) + "\n").encode())

    game.turn = "X"
    print("Game Start.")
    init_update = json.dumps({"type": "update", "state": game.get_state()}) + "\n"
    for c in clients: c.send(init_update.encode())

    def handle_client(conn, idx):
        role = "X" if idx == rps_winner else "O"
        while True:
            try:
                data = conn.recv(1024)
                if not data: break
                buffer = data.decode()
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if not line: continue
                    msg = json.loads(line)
                    if msg["type"] == "move":
                        with lock:
                            if game.is_valid_move(msg["data"]) and game.turn == role:
                                game.apply_move(msg["data"], role)
                                update = json.dumps({"type": "update", "state": game.get_state()}) + "\n"
                                for c in clients: c.send(update.encode())
            except: break
        os._exit(0)

    t1 = threading.Thread(target=handle_client, args=(clients[0], 0))
    t2 = threading.Thread(target=handle_client, args=(clients[1], 1))
    t1.start(); t2.start(); t1.join(); t2.join()

def run_client(ip, port):
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try: client.connect((ip, port))
    except: return

    buffer = ""
    def recv_json():
        nonlocal buffer
        while "\n" not in buffer:
            chunk = client.recv(1024).decode()
            if not chunk: return None
            buffer += chunk
        line, buffer = buffer.split("\n", 1)
        return json.loads(line)

    my_role = None
    while my_role is None:
        msg = recv_json()
        if not msg: return
        if msg["type"] == "rps_req":
            while True:
                os.system('cls' if os.name == 'nt' else 'clear')
                print("=== Rock Paper Scissors ===")
                flush_input()
                move = input("Enter (R)ock, (P)aper, (S)cissors: ").upper()
                if move in ['R','P','S']:
                    client.send((json.dumps({"type":"rps","move":move})+"\n").encode())
                    print("Waiting...")
                    break
        elif msg["type"] == "rps_result":
            res = msg["res"]
            if res == "draw":
                print("\033[93mIt's a Draw! Please input again.\033[0m") 
                time.sleep(2) 
            else:
                my_role = msg["role"]
                print(f"You {res}! You will play as {my_role}")
                time.sleep(2)

    game_class = globals()[ACTIVE_GAME_CLASS]
    game = game_class()
    
    def listener():
        while not game.game_over:
            try:
                msg = recv_json()
                if not msg: break
                if msg["type"] == "update":
                    state = msg["state"]
                    game.board = state["board"]
                    game.turn = state["turn"]
                    game.winner = state["winner"]
                    game.game_over = state["game_over"]
                    game.history_count = state.get("history_count", 0)
                    game.render(my_role)
            except: break
    
    threading.Thread(target=listener, daemon=True).start()
    game.render(my_role)

    while not game.game_over:
        if game.turn != my_role:
            time.sleep(0.1)
            continue
        
        flush_input()
        
        try:
            user_input = input(game.get_input_prompt())
        except: break
        
        if game.game_over: break

        valid, err = game.validate_client_input(user_input)
        if not valid:
            print(f"\033[91m[Warning] {err}\033[0m")
            time.sleep(1)
            game.render(my_role)
            continue

        req = json.dumps({"type": "move", "data": user_input}) + "\n"
        client.send(req.encode())
        game.turn = "WAITING_SERVER"
        game.render(my_role)

    time.sleep(0.5)
    show_checkout_screen(game, my_role)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=int)
    parser.add_argument('--client', action='store_true')
    parser.add_argument('--connect', nargs=2)
    args = parser.parse_args()

    if args.server: run_server(args.server)
    elif args.client: run_client(args.connect[0], int(args.connect[1]))