#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

Adafruit_SSD1306 d(128,64,&Wire,-1);
Preferences prefs;

#define BT_NAME "ESP32-CHEATS"
BLECharacteristic* txChar;
bool btConnected = false;
String btBuffer = "";

const uint8_t JOY_X  = 1;
const uint8_t JOY_Y  = 0;
const uint8_t JOY_SW = 10;

int16_t joyCX = 2230;
int16_t joyCY = 2185;

enum State { ST_MENU, ST_BRICK, ST_SNAKE, ST_PONG, ST_CHEATS, ST_ABOUT };
State state = ST_MENU;
uint8_t menuSel = 0;
const uint8_t MENU_ITEMS = 5;
const char* menuNames[MENU_ITEMS] = {"BRICK OUT","SNAKE","PONG","CHEATS","ABOUT"};

uint32_t lastSw=0, lastUp=0, lastDown=0;
bool prevSw=false, prevUp=false, prevDown=false;
#define DEBOUNCE 200

#define MAX_LINES 64
#define MAX_LEN   40

void cheatsAdd(const String& line){
  if(line.length() == 0) return;
  int cnt = prefs.getInt("cnt", 0);
  if(cnt >= MAX_LINES) return;
  String key = "l" + String(cnt);
  prefs.putString(key.c_str(), line.substring(0, MAX_LEN));
  prefs.putInt("cnt", cnt + 1);
}
void cheatsClear(){
  int cnt = prefs.getInt("cnt", 0);
  for(int i=0;i<cnt;i++){ String key = "l" + String(i); prefs.remove(key.c_str()); }
  prefs.putInt("cnt", 0);
}
String cheatsGet(int i){ String key = "l" + String(i); return prefs.getString(key.c_str(), ""); }
int cheatsCount(){ return prefs.getInt("cnt", 0); }

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s){ btConnected = true; }
  void onDisconnect(BLEServer* s){
    btConnected = false;
    delay(100);
    BLEDevice::startAdvertising();
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch){
    String value = ch->getValue().c_str();
    for(uint8_t i=0;i<value.length();i++){
      char c = value[i];
      if(c == '\n' || c == '\r'){
        if(btBuffer.length() > 0){
          btBuffer.trim();
          if(btBuffer == "CLEAR") cheatsClear();
          else if(btBuffer == "LIST"){
            int n = cheatsCount();
            String out = "Lines: " + String(n) + "\n";
            for(int i=0;i<n;i++) out += cheatsGet(i) + "\n";
            txChar->setValue(out.c_str());
            txChar->notify();
          } else cheatsAdd(btBuffer);
          btBuffer = "";
        }
      } else {
        btBuffer += c;
        if(btBuffer.length() > MAX_LEN) btBuffer = btBuffer.substring(0, MAX_LEN);
      }
    }
  }
};

void btInit(){
  BLEDevice::init(BT_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService* service = server->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

  BLECharacteristic* rxChar = service->createCharacteristic(
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCB());

  txChar = service->createCharacteristic(
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txChar->addDescriptor(new BLE2902());

  service->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  adv->setScanResponse(true);
  adv->start();
}

int16_t readX(){ int32_t s=0; for(uint8_t i=0;i<4;i++) s+=analogRead(JOY_X); return s/4; }
int16_t readY(){ int32_t s=0; for(uint8_t i=0;i<4;i++) s+=analogRead(JOY_Y); return s/4; }

bool swPressed(){
  uint32_t now = millis();
  bool sw = !digitalRead(JOY_SW);
  bool just = sw && !prevSw && (now - lastSw > DEBOUNCE);
  if(just) lastSw = now;
  prevSw = sw;
  return just;
}

bool stickUp(){
  uint32_t now = millis();
  bool up = readY() < joyCY - 700;
  bool just = up && !prevUp && (now - lastUp > DEBOUNCE);
  if(just) lastUp = now;
  prevUp = up;
  return just;
}

bool stickDown(){
  uint32_t now = millis();
  bool dn = readY() > joyCY + 700;
  bool just = dn && !prevDown && (now - lastDown > DEBOUNCE);
  if(just) lastDown = now;
  prevDown = dn;
  return just;
}

void drawMenu(){
  d.clearDisplay();
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(0,0);
  d.print(btConnected ? "== CONSOLE ==" : "= CONSOLE BT =");
  d.drawFastHLine(0,10,128,1);
  for(uint8_t i=0;i<MENU_ITEMS;i++){
    int16_t y = 16 + i*9;
    if(y > 60) break;
    d.setCursor(4, y);
    d.print(i == menuSel ? "> " : "  ");
    d.print(menuNames[i]);
  }
  d.display();
}

#define PADDLE_W 20
#define PADDLE_H 3
#define PADDLE_Y 60
#define BALL_R   2
#define BRICK_W 12
#define BRICK_H 5
#define BRICK_COLS 10
#define BRICK_ROWS 5
#define BRICK_TOP 8
#define BRICK_LEFT 4
#define BRICK_GAP 1

int16_t b_paddleX, b_ballX, b_ballY;
int8_t  b_ballDX, b_ballDY;
bool    b_ballLaunched;
bool    b_bricks[BRICK_ROWS][BRICK_COLS];
uint8_t b_bricksLeft;
uint16_t b_score;
uint8_t  b_lives;
bool     b_gameOver, b_win;
uint32_t b_lastFrame;

void brickReset(){
  b_score = 0; b_lives = 3; b_gameOver = false; b_win = false;
  b_bricksLeft = 0;
  for(uint8_t r=0;r<BRICK_ROWS;r++)
    for(uint8_t c=0;c<BRICK_COLS;c++){ b_bricks[r][c]=true; b_bricksLeft++; }
  b_paddleX = (128 - PADDLE_W)/2;
  b_ballX = b_paddleX + PADDLE_W/2;
  b_ballY = PADDLE_Y - BALL_R - 1;
  b_ballDX = b_ballDY = 0;
  b_ballLaunched = false;
  b_lastFrame = millis();
}

void brickLaunch(){
  if(b_ballLaunched) return;
  b_ballLaunched = true;
  b_ballDX = random(0,2) ? 1 : -1;
  b_ballDY = -1;
}

void brickUpdate(){
  int16_t delta = readX() - joyCX;
  if(delta > 250 || delta < -250){
    int16_t sp = delta / 350;
    if(sp >  5) sp =  5;
    if(sp < -5) sp = -5;
    b_paddleX += sp;
    if(b_paddleX < 0) b_paddleX = 0;
    if(b_paddleX > 128 - PADDLE_W) b_paddleX = 128 - PADDLE_W;
  }

  if(!b_ballLaunched){
    b_ballX = b_paddleX + PADDLE_W/2;
    b_ballY = PADDLE_Y - BALL_R - 1;
    return;
  }

  b_ballX += b_ballDX;
  b_ballY += b_ballDY;

  if(b_ballX <= BALL_R){ b_ballX = BALL_R; b_ballDX = -b_ballDX; }
  if(b_ballX >= 127 - BALL_R){ b_ballX = 127 - BALL_R; b_ballDX = -b_ballDX; }
  if(b_ballY <= BALL_R){ b_ballY = BALL_R; b_ballDY = -b_ballDY; }

  if(b_ballDY > 0 && b_ballY + BALL_R >= PADDLE_Y &&
     b_ballY - BALL_R <= PADDLE_Y + PADDLE_H &&
     b_ballX >= b_paddleX && b_ballX <= b_paddleX + PADDLE_W){
    b_ballY = PADDLE_Y - BALL_R - 1;
    b_ballDY = -b_ballDY;
    int16_t hit = b_ballX - (b_paddleX + PADDLE_W/2);
    b_ballDX = hit / 4;
    if(b_ballDX == 0) b_ballDX = (random(0,2) ? 1 : -1);
    if(b_ballDX >  2) b_ballDX =  2;
    if(b_ballDX < -2) b_ballDX = -2;
  }

  for(uint8_t r=0;r<BRICK_ROWS;r++){
    for(uint8_t c=0;c<BRICK_COLS;c++){
      if(!b_bricks[r][c]) continue;
      int16_t bx = BRICK_LEFT + c*(BRICK_W + BRICK_GAP);
      int16_t by = BRICK_TOP  + r*(BRICK_H + BRICK_GAP);
      if(b_ballX + BALL_R >= bx && b_ballX - BALL_R <= bx + BRICK_W &&
         b_ballY + BALL_R >= by && b_ballY - BALL_R <= by + BRICK_H){
        b_bricks[r][c] = false;
        b_bricksLeft--;
        b_score += 10;
        b_ballDY = -b_ballDY;
        if(b_ballDY == 0) b_ballDY = 1;
        return;
      }
    }
  }

  if(b_ballY > 63){
    b_lives--;
    if(b_lives == 0) b_gameOver = true;
    else {
      b_ballX = b_paddleX + PADDLE_W/2;
      b_ballY = PADDLE_Y - BALL_R - 1;
      b_ballDX = b_ballDY = 0;
      b_ballLaunched = false;
    }
  }
}

void brickDraw(){
  d.clearDisplay();
  for(uint8_t r=0;r<BRICK_ROWS;r++)
    for(uint8_t c=0;c<BRICK_COLS;c++){
      if(!b_bricks[r][c]) continue;
      int16_t bx = BRICK_LEFT + c*(BRICK_W + BRICK_GAP);
      int16_t by = BRICK_TOP  + r*(BRICK_H + BRICK_GAP);
      d.fillRect(bx, by, BRICK_W, BRICK_H, 1);
    }
  d.fillRect(b_paddleX, PADDLE_Y, PADDLE_W, PADDLE_H, 1);
  d.fillCircle(b_ballX, b_ballY, BALL_R, 1);
  d.fillRect(0,0,128,7,0);
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(0,0); d.print(b_score);
  d.setCursor(100,0); d.print("L:"); d.print(b_lives);
  d.display();
}

void brickLoop(){
  bool sw = swPressed();
  bool up = stickUp();

  if(b_gameOver || b_win){
    d.clearDisplay();
    d.setTextSize(2); d.setTextColor(1);
    d.setCursor(4,8);  d.print(b_win ? "YOU WIN" : "GAME");
    d.setCursor(4,28); d.print(b_win ? "" : "OVER");
    d.setTextSize(1);
    d.setCursor(4,52); d.print("SW=BACK UP=RESTART");
    d.display();
    if(sw){ delay(200); state = ST_MENU; }
    if(up){ delay(200); brickReset(); }
    return;
  }

  if(sw || up) brickLaunch();

  uint32_t now = millis();
  if(now - b_lastFrame >= 16){
    b_lastFrame = now;
    brickUpdate();
    if(b_bricksLeft == 0) b_win = true;
  }
  brickDraw();
}

#define S_CS 4
#define S_GW 32
#define S_GH 16
#define S_MAX 200

byte s_sx[S_MAX], s_sy[S_MAX];
byte s_len, s_fx, s_fy, s_score;
int8_t s_dx, s_dy, s_ndx, s_ndy;
bool s_over;
uint32_t s_lastMove;

void snakeFood(){
  bool ok;
  do{
    ok = 1;
    s_fx = random(S_GW); s_fy = random(S_GH);
    for(byte i=0;i<s_len;i++) if(s_sx[i]==s_fx && s_sy[i]==s_fy){ ok=0; break; }
  }while(!ok);
}

void snakeReset(){
  s_len = 3; s_score = 0; s_over = 0;
  s_dx = s_ndx = 1; s_dy = s_ndy = 0;
  for(byte i=0;i<s_len;i++){ s_sx[i]=S_GW/2-i; s_sy[i]=S_GH/2; }
  snakeFood();
  s_lastMove = millis();
}

void snakeLoop(){
  bool sw = swPressed();
  int16_t dx = readX() - joyCX;
  int16_t dy = readY() - joyCY;

  if(dx < -800 && s_dx == 0) { s_ndx = -1; s_ndy = 0; }
  if(dx >  800 && s_dx == 0) { s_ndx =  1; s_ndy = 0; }
  if(dy < -800 && s_dy == 0) { s_ndx = 0; s_ndy = -1; }
  if(dy >  800 && s_dy == 0) { s_ndx = 0; s_ndy =  1; }

  if(s_over){
    d.clearDisplay();
    d.setTextSize(2); d.setTextColor(1);
    d.setCursor(4,8);  d.print("GAME");
    d.setCursor(4,28); d.print("OVER");
    d.setTextSize(1);
    d.setCursor(4,52); d.print("SCORE: "); d.print(s_score);
    d.setCursor(4,58); d.print("SW=BACK UP=RESTART");
    d.display();
    if(sw){ delay(200); state = ST_MENU; }
    if(stickUp()){ delay(200); snakeReset(); }
    return;
  }

  uint32_t now = millis();
  uint8_t sp = 150 - s_score*3;
  if(sp < 60) sp = 60;

  if(now - s_lastMove >= sp){
    s_lastMove = now;
    s_dx = s_ndx; s_dy = s_ndy;
    int hx = s_sx[0] + s_dx;
    int hy = s_sy[0] + s_dy;
    if(hx<0 || hx>=S_GW || hy<0 || hy>=S_GH){ s_over = 1; return; }
    for(byte i=0;i<s_len-1;i++)
      if(s_sx[i]==hx && s_sy[i]==hy){ s_over = 1; return; }
    bool ate = (hx==s_fx && hy==s_fy);
    if(ate && s_len < S_MAX) s_len++;
    for(byte i=s_len-1;i>0;i--){ s_sx[i]=s_sx[i-1]; s_sy[i]=s_sy[i-1]; }
    s_sx[0]=hx; s_sy[0]=hy;
    if(ate){ s_score++; snakeFood(); }
  }

  d.clearDisplay();
  d.fillRect(s_fx*S_CS, s_fy*S_CS, S_CS-1, S_CS-1, 1);
  for(byte i=0;i<s_len;i++)
    d.fillRect(s_sx[i]*S_CS, s_sy[i]*S_CS, S_CS-1, S_CS-1, 1);
  d.fillRect(0,0,20,8,0);
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(1,0); d.print(s_score);
  d.display();
}

#define P_W 3
#define P_H 16
#define P_BALL_R 2
#define P_WIN_SCORE 5

int16_t p_myY, p_aiY;
int16_t p_bx, p_by;
int8_t  p_bdx, p_bdy;
uint8_t p_myScore, p_aiScore;
bool    p_over;
uint32_t p_lastFrame;

void pongReset(){
  p_myY = 24; p_aiY = 24;
  p_bx = 64; p_by = 32;
  p_bdx = random(0,2) ? 1 : -1;
  p_bdy = random(0,2) ? 1 : -1;
  p_myScore = 0; p_aiScore = 0;
  p_over = false;
  p_lastFrame = millis();
}

void pongUpdate(){
  int16_t dy = readY() - joyCY;
  if(dy < -300) p_myY -= 2;
  if(dy >  300) p_myY += 2;
  if(p_myY < 8) p_myY = 8;
  if(p_myY > 64 - P_H) p_myY = 64 - P_H;

  if(p_by < p_aiY + P_H/2 && p_aiY > 8) p_aiY -= 1;
  if(p_by > p_aiY + P_H/2 && p_aiY < 64 - P_H) p_aiY += 1;

  p_bx += p_bdx;
  p_by += p_bdy;

  if(p_by <= P_BALL_R + 8){ p_by = P_BALL_R + 8; p_bdy = -p_bdy; }
  if(p_by >= 63 - P_BALL_R){ p_by = 63 - P_BALL_R; p_bdy = -p_bdy; }

  if(p_bdx < 0 && p_bx - P_BALL_R <= 2 + P_W && p_bx - P_BALL_R >= 0 &&
     p_by >= p_myY && p_by <= p_myY + P_H){
    p_bx = 2 + P_W + P_BALL_R;
    p_bdx = -p_bdx;
    int16_t hit = p_by - (p_myY + P_H/2);
    if(hit < -4) p_bdy = -2;
    else if(hit > 4) p_bdy = 2;
  }

  if(p_bdx > 0 && p_bx + P_BALL_R >= 123 &&
     p_by >= p_aiY && p_by <= p_aiY + P_H){
    p_bx = 123 - P_BALL_R;
    p_bdx = -p_bdx;
  }

  if(p_bx < 0){
    p_aiScore++;
    p_bx = 64; p_by = 32;
    p_bdx = 1; p_bdy = random(0,2) ? 1 : -1;
    if(p_aiScore >= P_WIN_SCORE) p_over = true;
  }
  if(p_bx > 127){
    p_myScore++;
    p_bx = 64; p_by = 32;
    p_bdx = -1; p_bdy = random(0,2) ? 1 : -1;
    if(p_myScore >= P_WIN_SCORE) p_over = true;
  }
}

void pongDraw(){
  d.clearDisplay();
  d.drawFastHLine(0,8,128,1);
  d.drawFastHLine(0,63,128,1);
  d.fillRect(2, p_myY, P_W, P_H, 1);
  d.fillRect(123, p_aiY, P_W, P_H, 1);
  d.fillCircle(p_bx, p_by, P_BALL_R, 1);
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(50,0); d.print(p_myScore);
  d.setCursor(75,0); d.print(p_aiScore);
  d.display();
}

void pongLoop(){
  bool sw = swPressed();

  if(p_over){
    d.clearDisplay();
    d.setTextSize(2); d.setTextColor(1);
    d.setCursor(4,8);  d.print(p_myScore > p_aiScore ? "YOU WIN" : "AI WINS");
    d.setTextSize(1);
    d.setCursor(4,52); d.print("SW=BACK UP=RESTART");
    d.display();
    if(sw){ delay(200); state = ST_MENU; }
    if(stickUp()){ delay(200); pongReset(); }
    return;
  }

  uint32_t now = millis();
  if(now - p_lastFrame >= 20){ p_lastFrame = now; pongUpdate(); }
  pongDraw();
}

uint8_t cheatsTop = 0;

void cheatsLoop(){
  bool sw = swPressed();
  bool up = stickUp();
  bool dn = stickDown();

  if(sw){ delay(200); state = ST_MENU; return; }

  int total = cheatsCount();
  if(dn && cheatsTop + 7 < total) cheatsTop++;
  if(up && cheatsTop > 0) cheatsTop--;

  d.clearDisplay();
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(0,0);
  d.print("CHEATS ");
  d.print(total);
  d.print(btConnected ? " [BT]" : " [--]");
  d.drawFastHLine(0,10,128,1);

  if(total == 0){
    d.setCursor(0,20); d.print("Empty.");
    d.setCursor(0,30); d.print("Send via BLE:");
    d.setCursor(0,42); d.print("ESP32-CHEATS");
    d.setCursor(0,52); d.print("SW = BACK");
  } else {
    for(uint8_t i=0;i<6;i++){
      uint8_t idx = cheatsTop + i;
      if(idx >= total) break;
      d.setCursor(0, 13 + i*9);
      d.print(cheatsGet(idx));
    }
    d.setCursor(0, 58);
    d.print("SW=back  ");
    d.print(cheatsTop+1);
    d.print("/");
    d.print(total);
  }
  d.display();
}

void aboutLoop(){
  d.clearDisplay();
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(0,0);  d.print("ESP32-C3 CONSOLE");
  d.drawFastHLine(0,10,128,1);
  d.setCursor(0,14); d.print("Games: Brick/Snake");
  d.setCursor(0,24); d.print("Pong");
  d.setCursor(0,34); d.print("Cheats via BLE:");
  d.setCursor(0,44); d.print("ESP32-CHEATS");
  d.setCursor(0,56); d.print("SW = BACK");
  d.display();
  if(swPressed()){ delay(200); state = ST_MENU; }
}

void setup(){
  delay(50);
  Wire.begin(4, 5);
  d.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  d.clearDisplay(); d.display();
  pinMode(JOY_SW, INPUT_PULLUP);
  randomSeed(analogRead(2));

  prefs.begin("cheats", false);

  d.clearDisplay();
  d.setTextSize(1); d.setTextColor(1);
  d.setCursor(0,20); d.print("BT starting...");
  d.display();
  btInit();

  d.clearDisplay();
  d.setCursor(0,20); d.print("BT ready:");
  d.setCursor(0,32); d.print(BT_NAME);
  d.display();
  delay(800);

  state = ST_MENU;
}

void loop(){
  switch(state){
    case ST_MENU:
      if(stickUp())   { menuSel = (menuSel + MENU_ITEMS - 1) % MENU_ITEMS; delay(100); }
      if(stickDown()) { menuSel = (menuSel + 1) % MENU_ITEMS; delay(100); }
      if(swPressed()){
        delay(150);
        switch(menuSel){
          case 0: brickReset(); state = ST_BRICK;  break;
          case 1: snakeReset(); state = ST_SNAKE;  break;
          case 2: pongReset();  state = ST_PONG;   break;
          case 3: cheatsTop=0;  state = ST_CHEATS; break;
          case 4:               state = ST_ABOUT;  break;
        }
      }
      drawMenu();
      break;

    case ST_BRICK:  brickLoop();  break;
    case ST_SNAKE:  snakeLoop();  break;
    case ST_PONG:   pongLoop();   break;
    case ST_CHEATS: cheatsLoop(); break;
    case ST_ABOUT:  aboutLoop();  break;
  }
}
