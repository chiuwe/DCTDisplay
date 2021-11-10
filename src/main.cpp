#include <Arduino.h>
#include <stdint.h>
#include <Wire.h>
#include <SPI.h>
#include <Ra8876_Lite.h>
#include "Print.h"
#include "due_can.h"

#define BUTTON_SIZE 246
#define BUTTON_OFFSET 4
#define BUTTON_RADIUS 10
#define BUTTON_Y_START 348

#define DOT_RADIUS 10
#define DOT_SPACING 25

#define FT5316_INT 7

#define RA8876_XNSCS 10
#define RA8876_XNRESET 9

uint8_t addr = 0x38;

uint8_t dotLetters[9][2][35] =  {{{1,1,1,1,1,1,1,2,3,4,5,5,2,3,4,3,4,5},  // R x
                                 {1,2,3,4,5,6,7,1,1,1,2,3,4,4,4,5,6,7}},  // R y
                                {{1,1,1,1,1,1,1,2,3,4,5,5,5,5,5,5,5},     // N x
                                 {1,2,3,4,5,6,7,3,4,5,1,2,3,4,5,6,7}},    // N y
                                {{2,3,3,3,3,3,3,2,3,4,},                  // 1 x
                                 {2,1,2,3,4,5,6,7,7,7,}},                 // 1 y
                                {{1,2,3,4,5,5,4,3,2,1,2,3,4,5},           // 2 x
                                 {2,1,1,1,2,3,4,5,6,7,7,7,7,7}},          // 2 y
                                {{1,2,3,4,5,5,4,3,2,5,5,4,3,2,1},         // 3 x
                                 {1,1,1,1,2,3,4,4,4,5,6,7,7,7,7}},        // 3 y
                                {{1,1,1,1,2,3,4,5,5,5,5,5,5,5},           // 4 x
                                 {1,2,3,4,4,4,4,1,2,3,4,5,6,7}},          // 4 y
                                {{5,4,3,2,1,1,1,1,2,3,4,5,5,4,3,2,1},     // 5 x
                                 {1,1,1,1,1,2,3,4,4,4,4,5,6,7,7,7,7}},    // 5 y
                                {{4,3,2,1,1,1,1,1,2,3,4,5,5,4,3,2},       // 6 x
                                 {1,1,1,2,3,4,5,6,7,7,7,6,5,4,4,4}},      // 6 y
                                {{1,2,3,4,5,5,5,4,3,3,3},                 // 7 x
                                 {1,1,1,1,1,2,3,4,5,6,7}}};               // 7 y

struct TouchLocation {
  uint16_t x;
  uint16_t y;
};

enum Selector {Reverse, Neutral, Manual, Auto};

TouchLocation touchLocations[5];
uint16_t buttonXStart[4];
bool buttonDown[4];
uint8_t currentGear = -1;
CAN_FRAME outgoing;

Ra8876_Lite ra8876lite(RA8876_XNSCS, RA8876_XNRESET);

void drawButton(Selector select, uint16_t outColor, uint16_t inColor);
void drawReverse();
void drawNeutral();
void drawManual();
void drawAuto();
int drawDotLetter(uint8_t gear);

uint8_t readFT5316TouchRegister(uint8_t reg);
uint8_t readFT5316TouchLocation(TouchLocation *pLoc, uint8_t num);
uint8_t readFT5316TouchAddr(uint8_t regAddr, uint8_t * pBuf, uint8_t len);

void parseIncomingFrame(CAN_FRAME &frame);

void setup(){
  char str[] = "DCT Temp (C)";
  Serial.begin(9600);
  Wire.begin();

  // Setup CANbus
  Can0.init(CAN_BPS_500K);

  Can0.watchFor();

  // Setup outgoing can frame
  outgoing.id = 0x861;
  outgoing.extended = false;

  // Setup touchscreen
  // turn on backlight
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
  pinMode(FT5316_INT, INPUT);
  delay(100);
  ra8876lite.begin();
  ra8876lite.displayOn(true);
  ra8876lite.canvasImageStartAddress(PAGE1_START_ADDR);
  ra8876lite.canvasImageWidth(SCREEN_WIDTH);
  ra8876lite.activeWindowXY(0,0);
  ra8876lite.activeWindowWH(SCREEN_WIDTH, SCREEN_HEIGHT);
  ra8876lite.drawSquareFill(0, 0, 1023, 599, COLOR65K_BLACK);

  for(int i=0; i <4; i++){
    buttonDown[i] = false;
    buttonXStart[i] = 4 + (i * 256);
    drawButton((Selector)i, COLOR65K_BLUE2, COLOR65K_DARKBLUE);
  }
  drawDotLetter(0);

  ra8876lite.setTextParameter1(RA8876_SELECT_INTERNAL_CGROM,RA8876_CHAR_HEIGHT_32,RA8876_SELECT_8859_1);
  ra8876lite.setTextParameter2(RA8876_TEXT_FULL_ALIGN_DISABLE,RA8876_TEXT_CHROMA_KEY_DISABLE,RA8876_TEXT_WIDTH_ENLARGEMENT_X1,RA8876_TEXT_HEIGHT_ENLARGEMENT_X1);
  ra8876lite.putString(0,200,str);
  ra8876lite.textColor(COLOR65K_GREEN, COLOR65K_BLACK);
  ra8876lite.putDec(0, 230, 0, 3, "n");
}

void loop() {
  uint8_t attention = digitalRead(FT5316_INT);
  uint8_t nextGear = currentGear;
  CAN_FRAME incoming;

  if(Can0.available()){
    Can0.read(incoming);
    parseIncomingFrame(incoming);
  }

  if(!attention){
    uint8_t touch = readFT5316TouchLocation(touchLocations, 1);
    if(touch){
      Serial.println("Touching!");
      if(touchLocations[0].y >= BUTTON_Y_START){
        Serial.println("Touching a button!");
        for(int i=Reverse; i<=Auto; i++){
          uint16_t x = touchLocations[0].x;
          uint16_t start = buttonXStart[i];
          if(!buttonDown[i] && x >= start && x <= start + BUTTON_SIZE){
            drawButton((Selector)i, COLOR65K_DARKRED, COLOR65K_LIGHTRED);
            buttonDown[i] = true;
            outgoing.data.byte[0] = i;
            Can0.sendFrame(outgoing);
          }else if(buttonDown[i] && (x < start || x > start + BUTTON_SIZE)){
            drawButton((Selector)i, COLOR65K_BLUE2, COLOR65K_DARKBLUE);
            buttonDown[i] = false;
          }
        }
      }
    }else{
      for(int i=Reverse; i<=Auto; i++){
        if(buttonDown[i]){
          drawButton((Selector)i, COLOR65K_BLUE2, COLOR65K_DARKBLUE);
          buttonDown[i] = false;
        }
      }
    }
  }
  drawDotLetter(nextGear);
}

void parseIncomingFrame(CAN_FRAME &frame) {
  switch(frame.id){
    case 0x168:
      // current gear
      drawDotLetter(frame.data.byte[0]);
      // dct oil temp
      ra8876lite.textColor(COLOR65K_GREEN, COLOR65K_BLACK);
      ra8876lite.putDec(0, 230, frame.data.byte[1], 3, "n");
      break;
      // TODO: maybe print to screen frame ID?
    //default:
  }
}

uint8_t readFT5316TouchRegister(uint8_t reg){
  Wire.beginTransmission(addr);
  Wire.write(reg);
  uint8_t retVal = Wire.endTransmission();
  Wire.requestFrom(addr, uint8_t(1));
  if (Wire.available())
    retVal = Wire.read();

  return retVal;
}

uint8_t readFT5316TouchAddr(uint8_t regAddr, uint8_t * pBuf, uint8_t len){
  uint8_t i;
  Wire.beginTransmission(addr);
  Wire.write(regAddr);
  Wire.endTransmission();
  Wire.requestFrom(addr, len);
  for (i = 0; (i < len) && Wire.available(); i++)
    pBuf[i] = Wire.read();

  return i;
}

uint8_t readFT5316TouchLocation(TouchLocation *pLoc, uint8_t num){
  uint8_t i;
  uint8_t k;

  if (!pLoc) return 0; // must have a buffer
  if (!num)  return 0; // must be able to take at least one

  uint8_t status = readFT5316TouchRegister(0x02);

  static uint8_t tbuf[40];

  if((status & 0x0f) == 0) return 0; // no points detected

  uint8_t hitPoints = status & 0x0f;

  readFT5316TouchAddr(0x03, tbuf, hitPoints*6);

  for (k=0,i = 0; (i < hitPoints*6)&&(k < num); k++, i += 6)
  {
    pLoc[k].x = SCREEN_WIDTH - 1 - ((tbuf[i+0] & 0x0f) << 8 | tbuf[i+1]);
    pLoc[k].y = SCREEN_HEIGHT - 1 - ((tbuf[i+2] & 0x0f) << 8 | tbuf[i+3]);
  }

  return k;
}

int drawDotLetter(uint8_t gear){
  uint8_t *x = dotLetters[gear][0];
  uint8_t *y = dotLetters[gear][1];
  uint8_t i = 0;

  if(gear == currentGear || gear > 8) return -1;
  ra8876lite.drawSquareFill(0, 0, 5*DOT_SPACING+DOT_RADIUS, 7*DOT_SPACING+DOT_RADIUS, COLOR65K_BLACK);
  while(x[i]){
    ra8876lite.drawCircleFill(x[i]*DOT_SPACING-DOT_RADIUS, y[i]*DOT_SPACING-DOT_RADIUS, DOT_RADIUS, COLOR65K_YELLOW);
    i++;
  }
  currentGear = gear;

  return 0;
}

void drawButton(Selector select, uint16_t outColor, uint16_t inColor){
  uint16_t x = buttonXStart[select];
  uint16_t y = BUTTON_Y_START;
  uint16_t r = BUTTON_RADIUS;
  uint16_t s = BUTTON_SIZE;
  uint16_t b = BUTTON_OFFSET;

  ra8876lite.drawCircleSquareFill(x, y, x+s, y+s, r, r, outColor);
  ra8876lite.drawCircleSquareFill(x+b, y+b, x+s-b, y+s-b, r, r, inColor);
  switch(select){
    case Reverse:
      drawReverse();
      break;
    case Neutral:
      drawNeutral();
      break;
    case Manual:
      drawManual();
      break;
    case Auto:
      drawAuto();
      break;
  }
}

void drawReverse(){
  uint16_t x1, x2, x3, x4, y1, y2, y3;
  x1 = buttonXStart[Reverse] + 18;
  x2 = x1 + 120;
  x3 = x1 + 90;
  x4 = x2 + 90;
  y1 = BUTTON_Y_START + 123;
  y2 = BUTTON_Y_START + 54;
  y3 = y2 + 138;

  ra8876lite.drawTriangleFill(x1, y1, x2, y2, x2, y3, COLOR65K_BLACK);
  ra8876lite.drawTriangleFill(x3, y1, x4, y2, x4, y3, COLOR65K_BLACK);
}

void drawNeutral(){
  uint16_t x1, x2, x3, x4, y1, y2;
  x1 = buttonXStart[Neutral] + 14;
  x2 = x1 + 82;
  x3 = x2 + 55;
  x4 = x3 + 82;
  y1 = BUTTON_Y_START + 14;
  y2 = y1 + 218;
  ra8876lite.drawSquareFill(x1, y1, x2, y2, COLOR65K_BLACK);
  ra8876lite.drawSquareFill(x3, y1, x4, y2, COLOR65K_BLACK);
}

void drawManual(){
  uint16_t x1, x2, y1, y2, y3;
  x1 = buttonXStart[Manual] + 23;
  x2 = x1 + 200;
  y1 = BUTTON_Y_START + 7;
  y2 = y1 + 231;
  y3 = y1 + 115;
  ra8876lite.drawTriangleFill(x1, y1, x1, y2, x2, y3, COLOR65K_BLACK);
}

void drawAuto(){
  uint16_t x1, x2, x3, x4, x5, x6, y1, y2, y3;
  x1 = buttonXStart[Auto] + 15;
  x2 = x1 + 109;
  x3 = buttonXStart[Auto] + 98;
  x4 = x3 + 109;
  x5 = buttonXStart[Auto] + 184;
  x6 = buttonXStart[Auto] + 231;
  y1 = BUTTON_Y_START + 60;
  y2 = y1 + 126;
  y3 = y1 + 63;
  ra8876lite.drawTriangleFill(x1, y1, x1, y2, x2, y3, COLOR65K_BLACK);
  ra8876lite.drawTriangleFill(x3, y1, x3, y2, x4, y3, COLOR65K_BLACK);
  ra8876lite.drawSquareFill(x5, y1, x6, y2, COLOR65K_BLACK);
}
