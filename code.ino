// ================= LIBRARIES =================
#include "DHT.h"
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// ================= DEFINE =================
#define HYGROPIN        4
#define HYGROTYPE       DHT11
#define LIGHT_PIN       A0
#define SD_CS_PIN       10
#define LED_CLK_PIN     8
#define LED_DATA_PIN    9
#define BTN_VERT_PIN    2
#define BTN_ROUGE_PIN   3
#define HOLD_DURATION   5000UL
#define LCD_REFRESH     2000UL
#define LCD_PAGES       5
#define DHT_RETRY_MS    1100UL
#define DHT_MAX_ESSAIS  5
#define MSG_ACCUEIL_MS  2000UL
#define MSG_SORTIE_MS   1500UL
#define DS1307_ADDR     0x68
#define LCD_ADDR        0x3E
#define RGB_ADDR        0x62
#define GPS_RX_PIN      6
#define GPS_TX_PIN      7
#define GPS_BAUD        9600
#define DEFAULT_LOG_INTERVAL   10000UL
#define DEFAULT_FILE_MAX_SIZE  2048UL
#define DEFAULT_TIMEOUT        5000UL
#define FW_VERSION   "1.6"
#define FW_LOT       "LOT-2025-001"
#define CONFIG_TIMEOUT_MS  30000UL
#define CFG_CMD_TIMEOUT_MS 150UL
#define EEPROM_MAGIC       0xA6
#define EEPROM_ADDR_MAGIC  0
#define EEPROM_ADDR_DATA   1
#define MODE_STANDARD    0
#define MODE_ECO         1
#define MODE_MAINTENANCE 2
#define MODE_CONFIG      3
#define NA16  INT16_MIN
#define NA32  INT32_MIN
#define MAX_REV 9

// ================= SEUILS CAPTEURS (compile-time) =================
#define LUMIN_ACTIF     1
#define LUMIN_LOW       255
#define LUMIN_HIGH      768
#define TEMP_AIR_ACTIF  1
#define MIN_TEMP_AIR    (-10)
#define MAX_TEMP_AIR    60
#define HYGR_ACTIF      1
#define HYGR_MINT       0
#define HYGR_MAXT       50

// ================= FLAGS =================
static uint8_t gFlags = 0;
#define FLAG_SD_OK       0x01
#define FLAG_NMEA_VALIDE 0x02
#define FLAG_RTC_OK      0x04
#define FLAG_BVERT       0x08
#define FLAG_BROUGE      0x10
#define FLAG_SD_PLEINE   0x20
#define sdOK         (gFlags & FLAG_SD_OK)
#define rtcOK        (gFlags & FLAG_RTC_OK)
#define nmeaValide   (gFlags & FLAG_NMEA_VALIDE)
#define bVertAppuye  (gFlags & FLAG_BVERT)
#define bRougeAppuye (gFlags & FLAG_BROUGE)
#define SET_FLAG(f)  (gFlags |=  (f))
#define CLR_FLAG(f)  (gFlags &= ~(f))

// ================= STRUCTURES =================
// [NOUVEAU] Couleur RVB unifiée – remplace tous les triplets (r, g, b) épars
struct RVB         { uint8_t r, g, b; };
struct DateTimeRTC { uint16_t year; uint8_t month, day, hour, minute, second; };
struct Mesures     { int16_t humidite, temperature, lumiere, altitude, vitesse;
                     int32_t latitude, longitude; };
struct Config      { uint32_t logInterval, fileMaxSize, timeout; };

// ================= OBJETS =================
DHT            dht(HYGROPIN, HYGROTYPE);
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

// ================= CONFIG EEPROM =================
static Config gConfig;
static bool   gHygrIgnore = false;

static void configDefaut(Config* c) {
  c->logInterval = DEFAULT_LOG_INTERVAL;
  c->fileMaxSize = DEFAULT_FILE_MAX_SIZE;
  c->timeout     = DEFAULT_TIMEOUT;
}
static void eepromSave(const Config* c) {
  EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  const uint8_t* p = (const uint8_t*)c;
  for (uint8_t i = 0; i < sizeof(Config); i++)
    EEPROM.write(EEPROM_ADDR_DATA + i, p[i]);
}
static void eepromLoad(Config* c) {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC) {
    configDefaut(c); eepromSave(c); return;
  }
  uint8_t* p = (uint8_t*)c;
  for (uint8_t i = 0; i < sizeof(Config); i++)
    p[i] = EEPROM.read(EEPROM_ADDR_DATA + i);
  if (c->logInterval < 1000UL   || c->logInterval > 86400000UL) c->logInterval = DEFAULT_LOG_INTERVAL;
  if (c->fileMaxSize < 512UL    || c->fileMaxSize  > 65535UL)   c->fileMaxSize = DEFAULT_FILE_MAX_SIZE;
  if (c->timeout     < 1000UL   || c->timeout      > 30000UL)   c->timeout     = DEFAULT_TIMEOUT;
}

// ================= LED P9813 =================
static void p9813SendByte(uint8_t val) {
  for (int8_t b = 7; b >= 0; b--) {
    digitalWrite(LED_DATA_PIN, (val >> b) & 1);
    digitalWrite(LED_CLK_PIN, 1);
    digitalWrite(LED_CLK_PIN, 0);
  }
}
static void p9813SendZeros() { for (uint8_t i = 0; i < 4; i++) p9813SendByte(0); }

// [MODIFIÉ] Prend un RVB au lieu de trois uint8_t séparés
static void ledCouleur(RVB c) {
  uint8_t ctrl = 0xC0 | ((~c.b>>6)&3)<<4 | ((~c.g>>6)&3)<<2 | ((~c.r>>6)&3);
  p9813SendZeros();
  p9813SendByte(ctrl); p9813SendByte(c.b); p9813SendByte(c.g); p9813SendByte(c.r);
  p9813SendZeros();
}

// ================= LCD I2C =================
static void rgbWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(RGB_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

// [MODIFIÉ] Prend un RVB au lieu de trois uint8_t séparés
static void lcdSetRGB(RVB c) {
  rgbWrite(0x00,0); rgbWrite(0x01,0); rgbWrite(0x08,0xAA);
  rgbWrite(0x04,c.r); rgbWrite(0x03,c.g); rgbWrite(0x02,c.b);
}
static void lcdCmd(uint8_t cmd) {
  Wire.beginTransmission(LCD_ADDR); Wire.write(0x80); Wire.write(cmd); Wire.endTransmission();
  delayMicroseconds(50);
}
static void lcdData(uint8_t c) {
  Wire.beginTransmission(LCD_ADDR); Wire.write(0x40); Wire.write(c); Wire.endTransmission();
  delayMicroseconds(50);
}
static void lcdInit() {
  delay(50);
  lcdCmd(0x38); lcdCmd(0x39); lcdCmd(0x14); lcdCmd(0x70); lcdCmd(0x56); lcdCmd(0x6C);
  delay(200);
  lcdCmd(0x38); lcdCmd(0x0C); lcdCmd(0x01);
  delay(2);
}
static void lcdClear()                             { lcdCmd(0x01); delay(2); }
static void lcdSetCursor(uint8_t col, uint8_t row) { lcdCmd(0x80|(col+(row?0x40:0))); }
static void lcdPrintP(const __FlashStringHelper* s) {
  PGM_P p = reinterpret_cast<PGM_P>(s); char c;
  while ((c = pgm_read_byte(p++))) lcdData(c);
}
static void lcdPrintInt(int32_t val) {
  if (!val) { lcdData('0'); return; }
  if (val<0) { lcdData('-'); val=-val; }
  char buf[12]; int8_t i=0;
  while (val>0) { buf[i++]='0'+val%10; val/=10; }
  while (i--) lcdData(buf[i]);
}
static void lcdPrintStr(const char* s) { while (*s) lcdData(*s++); }

// ================= RTC DS1307 =================
static uint8_t bcdToDec(uint8_t v) { return (v>>4)*10+(v&0x0F); }
static uint8_t decToBcd(uint8_t v) { return ((v/10)<<4)|(v%10); }

static DateTimeRTC rtcNow() {
  DateTimeRTC dt = {2000,1,1,0,0,0};
  Wire.beginTransmission(DS1307_ADDR); Wire.write(0x00);
  if (Wire.endTransmission()!=0) return dt;
  Wire.requestFrom((uint8_t)DS1307_ADDR,(uint8_t)7);
  dt.second = bcdToDec(Wire.read()&0x7F);
  dt.minute = bcdToDec(Wire.read());
  dt.hour   = bcdToDec(Wire.read()&0x3F);
  Wire.read();
  dt.day    = bcdToDec(Wire.read());
  dt.month  = bcdToDec(Wire.read());
  dt.year   = 2000+bcdToDec(Wire.read());
  return dt;
}
static void rtcAdjust(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  Wire.beginTransmission(DS1307_ADDR); Wire.write(0x00);
  Wire.write(decToBcd(s)); Wire.write(decToBcd(mi)); Wire.write(decToBcd(h));
  Wire.write(0x01);
  Wire.write(decToBcd(d)); Wire.write(decToBcd(mo)); Wire.write(decToBcd(y%100));
  Wire.endTransmission();
}
static void rtcSetDOW(uint8_t dow) {
  Wire.beginTransmission(DS1307_ADDR); Wire.write(0x03); Wire.write(decToBcd(dow)); Wire.endTransmission();
}
static bool rtcIsRunning() {
  Wire.beginTransmission(DS1307_ADDR); Wire.write(0x00); Wire.endTransmission();
  Wire.requestFrom((uint8_t)DS1307_ADDR,(uint8_t)1);
  return !(Wire.read()&0x80);
}
static void rtcBegin() {
  Wire.beginTransmission(DS1307_ADDR);
  if (Wire.endTransmission()!=0) return;
  SET_FLAG(FLAG_RTC_OK);
  if (rtcIsRunning()) return;
  const char* d = __DATE__; const char* t = __TIME__;
  uint8_t mo = 1;
  const char* mois = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (uint8_t i=0; i<12; i++)
    if (d[0]==mois[i*3]&&d[1]==mois[i*3+1]&&d[2]==mois[i*3+2]) { mo=i+1; break; }
  uint16_t y  = (d[7]-'0')*1000+(d[8]-'0')*100+(d[9]-'0')*10+(d[10]-'0');
  uint8_t  dd = (d[4]==' '?0:d[4]-'0')*10+(d[5]-'0');
  uint8_t  hh = (t[0]-'0')*10+(t[1]-'0'), mi=(t[3]-'0')*10+(t[4]-'0'), ss=(t[6]-'0')*10+(t[7]-'0');
  rtcAdjust(y,mo,dd,hh,mi,ss);
}

// ================= PARSEUR NMEA MINIMALISTE =================
#define NMEA_BUF 72
static char    nmeaBuf[NMEA_BUF];
static uint8_t nmeaIdx = 0;
static int32_t nmeaLat = NA32, nmeaLon = NA32;
static int16_t nmeaAlt = NA16, nmeaVit = NA16;
static bool    gpsDebug = false;

static uint8_t nmeaField(const char* s, uint8_t n) {
  uint8_t cnt=0,i=0; while(s[i]){ if(s[i++]==','){ if(++cnt==n) return i; } } return 0;
}
static int32_t nmeaCoord(const char* s, uint8_t pos) {
  uint8_t pt=pos; while(s[pt]&&s[pt]!='.') pt++;
  int32_t deg=0; for(uint8_t i=pos;i<pt-2;i++) deg=deg*10+(s[i]-'0');
  int32_t mEnt=0; for(uint8_t i=pt-2;i<pt;i++) mEnt=mEnt*10+(s[i]-'0');
  int32_t mDec=0; for(uint8_t i=1;i<=4;i++){ char c=s[pt+i]; mDec=mDec*10+(c>='0'&&c<='9'?c-'0':0); }
  return deg*10000L+(mEnt*10000L+mDec)/60L;
}
static void nmeaParse(const char* tr) {
  uint8_t cs=0,i=1; while(tr[i]&&tr[i]!='*') cs^=tr[i++];
  if(tr[i]!='*') return;
  char h=tr[i+1], l=tr[i+2];
  uint8_t ref = ((h>='A'?h-'A'+10:h-'0')<<4)|(l>='A'?l-'A'+10:l-'0');
  if(cs!=ref) return;
  if(tr[3]=='R'&&tr[4]=='M'&&tr[5]=='C'){
    uint8_t f2=nmeaField(tr,2); if(tr[f2]!='A') return;
    uint8_t fLat=nmeaField(tr,3), fNS=nmeaField(tr,4);
    uint8_t fLon=nmeaField(tr,5), fEW=nmeaField(tr,6), fV=nmeaField(tr,7);
    nmeaLat=nmeaCoord(tr,fLat); if(tr[fNS]=='S') nmeaLat=-nmeaLat;
    nmeaLon=nmeaCoord(tr,fLon); if(tr[fEW]=='W') nmeaLon=-nmeaLon;
    int32_t vn=0; uint8_t vi=fV;
    while(tr[vi]&&tr[vi]!=','&&tr[vi]!='.') vn=vn*10+(tr[vi++]-'0');
    nmeaVit=(int16_t)(vn*514L/1000L);
    SET_FLAG(FLAG_NMEA_VALIDE);
  }
  if(tr[3]=='G'&&tr[4]=='G'&&tr[5]=='A'){
    uint8_t f6=nmeaField(tr,6); if(tr[f6]=='0') return;
    uint8_t fA=nmeaField(tr,9);
    int32_t a=0; uint8_t ai=fA;
    while(tr[ai]&&tr[ai]!=','&&tr[ai]!='.') a=a*10+(tr[ai++]-'0');
    nmeaAlt=(int16_t)a;
  }
}
static void nmeaProcess(char c) {
  if(c=='$') nmeaIdx=0;
  if(nmeaIdx<NMEA_BUF-1) nmeaBuf[nmeaIdx++]=c;
  if(c=='\n'&&nmeaIdx>6){
    nmeaBuf[nmeaIdx]='\0';
    nmeaParse(nmeaBuf);
    if(gpsDebug) Serial.print(nmeaBuf);
  }
}
static void gpsLireOctets()       { while(gpsSerial.available()) nmeaProcess(gpsSerial.read()); }
static void viderGPS(Mesures* m)  { m->latitude=m->longitude=NA32; m->altitude=m->vitesse=NA16; }

// ================= VARIABLES GLOBALES =================
unsigned long lastLogTime        = 0;
uint8_t       compteurEco        = 0;
uint8_t       modeCourant        = MODE_STANDARD;
uint8_t       modeAvant          = MODE_STANDARD;
uint8_t       lcdPage            = 0;
uint8_t       etatMaint          = 0;
uint8_t       etatLecture        = 0;
unsigned long dernierRafraichLCD = 0;
unsigned long tEtatMaint         = 0;
unsigned long tVertAppui         = 0;
unsigned long tRougeAppui        = 0;

static uint8_t  erreursDHT = 0;
static uint8_t  erreursGPS = 0;
static uint8_t  erreursSD  = 0;

static Mesures  gMesures;
static Mesures* cibleLecture = nullptr;
static char     gFileBuf[13];

// ================= ISR =================
void ISR_vert()  { SET_FLAG(FLAG_BVERT);  }
void ISR_rouge() { SET_FLAG(FLAG_BROUGE); }

// ================= HELPERS =================
static int16_t floatToInt16x10(float v) {
  return isnan(v) ? NA16 : (int16_t)(v*10.0f+(v>=0?0.5f:-0.5f));
}

// ================= CAPTEURS =================
bool lireDHT(Mesures* m) {
  static unsigned long tD=0; static uint8_t ess=0;
  unsigned long now=millis();
  if(now-tD>=DHT_RETRY_MS){
    tD=now;
    float h=dht.readHumidity(), t=dht.readTemperature();
    if(!isnan(h)&&!isnan(t)){
      ess=0; erreursDHT=0;
#if TEMP_AIR_ACTIF
      m->temperature = (t<(float)MIN_TEMP_AIR||t>(float)MAX_TEMP_AIR) ? NA16 : floatToInt16x10(t);
#else
      m->temperature = NA16;
#endif
#if HYGR_ACTIF
      if(t<(float)HYGR_MINT||t>(float)HYGR_MAXT){ m->humidite=NA16; gHygrIgnore=true; }
      else { m->humidite=floatToInt16x10(h); gHygrIgnore=false; }
#else
      m->humidite=NA16; gHygrIgnore=false;
#endif
      return true;
    }
    if(++ess>=DHT_MAX_ESSAIS){
      m->humidite=m->temperature=NA16; gHygrIgnore=false;
      ess=0; if(erreursDHT<255) erreursDHT++;
      return true;
    }
  }
  return false;
}

void lireLumiere(Mesures* m) {
#if LUMIN_ACTIF
  m->lumiere=(int16_t)analogRead(LIGHT_PIN);
#else
  m->lumiere=NA16;
#endif
}

bool lireGPS(Mesures* m) {
  static unsigned long tG=0; if(!tG) tG=millis();
  gpsLireOctets();
  if(nmeaValide){
    m->latitude=nmeaLat; m->longitude=nmeaLon; m->altitude=nmeaAlt; m->vitesse=nmeaVit;
    CLR_FLAG(FLAG_NMEA_VALIDE); tG=0; erreursGPS=0;
    return true;
  }
  if(millis()-tG>=gConfig.timeout){
    viderGPS(m); tG=0; if(erreursGPS<255) erreursGPS++;
    return true;
  }
  return false;
}

// ================= LECTURE NON BLOQUANTE =================
void demarrerLecture(Mesures* m) { cibleLecture=m; etatLecture=1; }
bool tickLecture() {
  if(!etatLecture||!cibleLecture) return false;
  switch(etatLecture){
    case 1: if(lireDHT(cibleLecture)){ lireLumiere(cibleLecture); etatLecture=2; } break;
    case 2: if(lireGPS(cibleLecture)) etatLecture=3; break;
    case 3: etatLecture=0; cibleLecture=nullptr; return true;
  }
  return false;
}

// ================= HORODATAGE =================
static void writeTwo(char* b, uint8_t p, uint8_t v){ b[p]='0'+v/10; b[p+1]='0'+v%10; }
const char* getTimestamp() {
  static char ts[20]; if(!rtcOK) return "NA";
  DateTimeRTC n=rtcNow();
  ts[0]='0'+n.year/1000; ts[1]='0'+n.year/100%10; ts[2]='0'+n.year/10%10; ts[3]='0'+n.year%10; ts[4]='-';
  writeTwo(ts,5,n.month); ts[7]='-'; writeTwo(ts,8,n.day); ts[10]=' ';
  writeTwo(ts,11,n.hour); ts[13]=':'; writeTwo(ts,14,n.minute); ts[16]=':'; writeTwo(ts,17,n.second); ts[19]='\0';
  return ts;
}
const char* getHeureSeule() { return rtcOK ? getTimestamp()+11 : "NA"; }

// ================= HELPERS SD =================
static void writeI16orNA(File& f, int16_t v){
  if(v==NA16){ f.print(F("NA")); return; } if(v<0){ f.print('-'); v=-v; } f.print(v/10); f.print('.'); f.print(v%10);
}
static void writeI32orNA(File& f, int32_t v){
  if(v==NA32){ f.print(F("NA")); return; } if(v<0){ f.print('-'); v=-v; }
  f.print(v/10000L); f.print('.');
  int32_t d=v%10000L; if(d<1000)f.print('0'); if(d<100)f.print('0'); if(d<10)f.print('0'); f.print(d);
}
static void getDateParts(uint8_t& yy, uint8_t& mm, uint8_t& dd){
  if(rtcOK){ DateTimeRTC n=rtcNow(); yy=n.year%100; mm=n.month; dd=n.day; } else yy=mm=dd=0;
}
static void buildFileName(char* buf, uint8_t yy, uint8_t mm, uint8_t dd, uint8_t rev){
  buf[0]='0'+yy/10; buf[1]='0'+yy%10; buf[2]='0'+mm/10; buf[3]='0'+mm%10;
  buf[4]='0'+dd/10; buf[5]='0'+dd%10; buf[6]='_'; buf[7]='0'+(rev%10);
  buf[8]='.'; buf[9]='L'; buf[10]='O'; buf[11]='G'; buf[12]='\0';
}
static bool copierFichier(const char* src, const char* dst){
  File in=SD.open(src,FILE_READ); if(!in) return false;
  File out=SD.open(dst,FILE_WRITE); if(!out){ in.close(); return false; }
  uint8_t bloc[16]; size_t lu;
  while((lu=in.read(bloc,sizeof(bloc)))>0) out.write(bloc,lu);
  in.close(); out.close(); return true;
}
static void rotateFichier(uint8_t yy, uint8_t mm, uint8_t dd){
  char src[13], dst[13]; buildFileName(src,yy,mm,dd,0);
  uint8_t rev=1;
  while(rev<=MAX_REV){ buildFileName(dst,yy,mm,dd,rev); if(!SD.exists(dst)) break; rev++; }
  if(rev>MAX_REV){ buildFileName(dst,yy,mm,dd,MAX_REV); SD.remove(dst); }
  if(copierFichier(src,dst)) SD.remove(src);
}

// ================= LOG SD =================
void logSD(const Mesures* m){
  if(!sdOK) return;
  uint8_t yy,mm,dd; getDateParts(yy,mm,dd); buildFileName(gFileBuf,yy,mm,dd,0);
  if(SD.exists(gFileBuf)){
    File chk=SD.open(gFileBuf,FILE_READ);
    if(chk){
      uint32_t sz=chk.size(); chk.close();
      if(sz>=gConfig.fileMaxSize){
        char dst[13]; uint8_t rev=1;
        while(rev<=MAX_REV){ buildFileName(dst,yy,mm,dd,rev); if(!SD.exists(dst)) break; rev++; }
        if(rev>MAX_REV) SET_FLAG(FLAG_SD_PLEINE);
        rotateFichier(yy,mm,dd);
      }
    }
  }
  File f=SD.open(gFileBuf,FILE_WRITE);
  if(!f){
    if(SD.begin(SD_CS_PIN)) SET_FLAG(FLAG_SD_OK); else CLR_FLAG(FLAG_SD_OK);
    if(erreursSD<255) erreursSD++; return;
  }
  if(f.size()==0) f.println(F("ts,hum,tmp,lum,lat,lon,alt,vit"));
  f.print(getTimestamp());        f.print(',');
  writeI16orNA(f,m->humidite);    f.print(',');
  writeI16orNA(f,m->temperature); f.print(',');
  f.print(m->lumiere);            f.print(',');
  writeI32orNA(f,m->latitude);    f.print(',');
  writeI32orNA(f,m->longitude);   f.print(',');
  writeI16orNA(f,m->altitude);    f.print(',');
  writeI16orNA(f,m->vitesse);
  f.println(); f.close();
  erreursSD=0;
}

// ================= LCD HELPERS =================
static void lcdI16x10(int16_t v){
  if(v==NA16){ lcdPrintP(F("NA")); return; } if(v<0){ lcdData('-'); v=-v; }
  lcdPrintInt(v/10); lcdData('.'); lcdPrintInt(v%10);
}
static void lcdCoord(int32_t v){
  if(v==NA32){ lcdPrintP(F("NA")); return; } if(v<0){ lcdData('-'); v=-v; }
  lcdPrintInt(v/10000L); lcdData('.');
  int32_t d=(v%10000L)/100L; if(d<10) lcdData('0'); lcdPrintInt(d);
}

// ================= LCD PAGES – TABLE DE POINTEURS =================
//
// [NOUVEAU] Chaque page est une fonction dédiée, dispatchée via un tableau
// de pointeurs en PROGMEM. Supprime le switch de l'ancienne lcdAfficherPage().
//
static void lcdPage0() {
  lcdSetCursor(0,0); lcdPrintP(F("Hum: ")); lcdI16x10(gMesures.humidite);    lcdPrintP(F(" %"));
  lcdSetCursor(0,1); lcdPrintP(F("Tmp: ")); lcdI16x10(gMesures.temperature); lcdPrintP(F(" C"));
}
static void lcdPage1() {
  lcdSetCursor(0,0); lcdPrintP(F("Lum: "));
  (gMesures.lumiere==NA16) ? lcdPrintP(F("NA")) : lcdPrintInt(gMesures.lumiere);
  lcdSetCursor(0,1); lcdPrintP(F("Heure:")); lcdPrintStr(getHeureSeule());
}
static void lcdPage2() {
  lcdSetCursor(0,0); lcdPrintP(F("Lat: ")); lcdCoord(gMesures.latitude);
  lcdSetCursor(0,1); lcdPrintP(F("Lon: ")); lcdCoord(gMesures.longitude);
}
static void lcdPage3() {
  lcdSetCursor(0,0); lcdPrintP(F("Alt: ")); lcdI16x10(gMesures.altitude); lcdPrintP(F("m"));
  lcdSetCursor(0,1); lcdPrintP(F("Vit: ")); lcdI16x10(gMesures.vitesse);  lcdPrintP(F("m/s"));
}
static void lcdPage4() {
  lcdSetCursor(0,0); lcdPrintP(sdOK ? F("SD:OK") : F("SD:ABSENT"));
  lcdSetCursor(0,1); lcdPrintP(F("Mode:")); lcdPrintP(modeAvant==MODE_ECO ? F("ECO") : F("STD"));
}

typedef void (*LcdPageFn)();
static const LcdPageFn PROGMEM kLcdPages[LCD_PAGES] = {
  lcdPage0, lcdPage1, lcdPage2, lcdPage3, lcdPage4
};

// [MODIFIÉ] Dispatch via table – O(1), zéro switch
void lcdAfficherPage(uint8_t page) {
  if (page >= LCD_PAGES) return;
  lcdClear();
  LcdPageFn fn = (LcdPageFn)pgm_read_word(&kLcdPages[page]);
  fn();
}

// ================= COULEURS DES MODES – TABLE PROGMEM =================
//
// [NOUVEAU] Remplace le switch dans ledModeCouleur().
// Indexé directement par modeCourant (0-3).
//
static const RVB PROGMEM kModeCouleurs[4] = {
  {  0, 255,   0},  // MODE_STANDARD
  {  0,   0, 255},  // MODE_ECO
  {255, 165,   0},  // MODE_MAINTENANCE
  {255, 255,   0},  // MODE_CONFIG
};

static void ledModeCouleur() {
  RVB c; memcpy_P(&c, &kModeCouleurs[modeCourant & 3], sizeof(RVB));
  ledCouleur(c);
}

// ================= ALERTES LED – TABLE PROGMEM =================
//
// Codes LED (fréquence ~1 Hz) :
//  Condition                  | Phase rouge | Phase couleur | Couleur
//  ---------------------------|-------------|---------------|--------
//  RTC absente                |   500 ms    |    500 ms     | Bleu
//  GPS timeout / absent       |   500 ms    |    500 ms     | Jaune
//  Erreur accès capteur (DHT) |   500 ms    |    500 ms     | Vert
//  Données incohérentes       |   500 ms    |   1000 ms     | Vert  (2×)
//  Carte SD pleine            |   500 ms    |    500 ms     | Blanc
//  Erreur accès / écriture SD |   500 ms    |   1000 ms     | Blanc (2×)
//
// [NOUVEAU] AlerteDef regroupe couleur + durée de phase couleur.
// Supprime le switch de l'ancienne tickAlertes().
//
#define PROB_AUCUN    0
#define PROB_DHT      1
#define PROB_GPS      2
#define PROB_SD       3
#define PROB_SD_PLEIN 4
#define PROB_INCO     5
#define PROB_RTC      6

struct AlerteDef { uint8_t r, g, b; uint16_t phaseCouleur; };
static const AlerteDef PROGMEM kAlerteDefs[] = {
  {  0,   0,   0,    0},  // PROB_AUCUN    (index 0, non utilisé)
  {  0, 255,   0,  500},  // PROB_DHT      (index 1)
  {255, 255,   0,  500},  // PROB_GPS      (index 2)
  {255, 255, 255, 1000},  // PROB_SD       (index 3)
  {255, 255, 255,  500},  // PROB_SD_PLEIN (index 4)
  {  0, 255,   0, 1000},  // PROB_INCO     (index 5)
  {  0,   0, 255,  500},  // PROB_RTC      (index 6)
};

static uint8_t       gAlerte          = PROB_AUCUN;
static bool          gAlertePhase     = false;
static unsigned long gAlerteT         = 0;
static uint8_t       gAlerteModeCache = 0xFF;

// detecterProbleme : vérifie valeurs hors-plage et incohérences croisées.
static uint8_t detecterProbleme(const Mesures* m) {
#if TEMP_AIR_ACTIF
  if (m->temperature != NA16 && (m->temperature < -400 || m->temperature > 850))
    return PROB_DHT;
#endif
#if HYGR_ACTIF
  if (!gHygrIgnore && m->humidite != NA16 && (m->humidite < 0 || m->humidite > 1000))
    return PROB_DHT;
#endif
#if LUMIN_ACTIF
  if (m->lumiere != NA16 && (m->lumiere < 0 || m->lumiere > 1023))
    return PROB_INCO;
#endif
#if HYGR_ACTIF && LUMIN_ACTIF
  if (!gHygrIgnore && m->humidite != NA16 && m->lumiere != NA16 && m->lumiere != 0)
    if (m->humidite > 950 && m->lumiere > 900) return PROB_INCO;
#endif
#if TEMP_AIR_ACTIF && LUMIN_ACTIF
  if (m->temperature != NA16 && m->lumiere != NA16)
    if (m->temperature > 250 && m->lumiere < 100) return PROB_INCO;
#endif
#if TEMP_AIR_ACTIF && HYGR_ACTIF
  if (!gHygrIgnore && m->temperature != NA16 && m->humidite != NA16)
    if (m->temperature < 0 && m->humidite < 100) return PROB_INCO;
#endif
  return PROB_AUCUN;
}

void majAlertes(const Mesures* m) {

  // Ping RTC toutes les 2 s
  {
    static unsigned long tRtcPing = 0;
    unsigned long nowP = millis();
    if (nowP - tRtcPing >= 2000UL) {
      tRtcPing = nowP;
      Wire.beginTransmission(DS1307_ADDR);
      if (Wire.endTransmission() == 0) SET_FLAG(FLAG_RTC_OK);
      else                             CLR_FLAG(FLAG_RTC_OK);
    }
  }

  uint8_t nouv = PROB_AUCUN;

  if      (!rtcOK)                                                  nouv = PROB_RTC;
  else if (erreursDHT >= 1)                                         nouv = PROB_DHT;
  else if (modeCourant != MODE_CONFIG && erreursGPS >= 1)           nouv = PROB_GPS;
  else if (modeCourant != MODE_MAINTENANCE && modeCourant != MODE_CONFIG) {
    if      (erreursSD  >= 1)          nouv = PROB_SD;
    else if (gFlags & FLAG_SD_PLEINE)  nouv = PROB_SD_PLEIN;
  }

  if (nouv == PROB_AUCUN) {
    uint8_t dp = detecterProbleme(m);
    if (dp == PROB_GPS &&
       (modeCourant == MODE_CONFIG || modeCourant == MODE_MAINTENANCE)) dp = PROB_AUCUN;
    if ((dp == PROB_SD || dp == PROB_SD_PLEIN) &&
       (modeCourant == MODE_MAINTENANCE || modeCourant == MODE_CONFIG)) dp = PROB_AUCUN;
    nouv = dp;
  }

  bool alerteChangee = (nouv != gAlerte);
  bool modeChange    = (nouv == PROB_AUCUN && modeCourant != gAlerteModeCache);
  if (!alerteChangee && !modeChange) return;

  gAlerte          = nouv;
  gAlerteModeCache = modeCourant;
  gAlertePhase     = false;
  gAlerteT         = millis();

  if (nouv == PROB_AUCUN) ledModeCouleur();
  else                    ledCouleur({255, 0, 0});
}

// [MODIFIÉ] Lecture de la table kAlerteDefs au lieu d'un switch
void tickAlertes() {
  if (gAlerte == PROB_AUCUN) return;

  AlerteDef def; memcpy_P(&def, &kAlerteDefs[gAlerte], sizeof(AlerteDef));
  const RVB col = {def.r, def.g, def.b};

  unsigned long now   = millis();
  unsigned long duree = gAlertePhase ? (unsigned long)def.phaseCouleur : 500UL;
  if (now - gAlerteT >= duree) {
    gAlertePhase = !gAlertePhase;
    gAlerteT     = now;
    if (gAlertePhase) ledCouleur(col);
    else              ledCouleur({255, 0, 0});
  }
}

// ================= MAINTENANCE =================
void tickMaintenance(){
  unsigned long now=millis();
  switch(etatMaint){
    case 1:
      if(now-tEtatMaint>=MSG_ACCUEIL_MS){
        lireLumiere(&gMesures); viderGPS(&gMesures);
        etatMaint=2; tEtatMaint=now;
      }
      break;
    case 2:
      if(lireDHT(&gMesures)){
        lcdPage=0; dernierRafraichLCD=now; lcdAfficherPage(0); etatMaint=3;
      }
      break;
    case 3:
      if(now-dernierRafraichLCD>=LCD_REFRESH){
        dernierRafraichLCD=now;
        lcdPage=(lcdPage+1)%LCD_PAGES;
        if(lcdPage==2){ gpsLireOctets(); lireGPS(&gMesures); }
        else           { lireDHT(&gMesures); lireLumiere(&gMesures); }
        lcdAfficherPage(lcdPage);
      }
      break;
    case 4:
      if(now-tEtatMaint>=MSG_SORTIE_MS){
        if(SD.begin(SD_CS_PIN)) SET_FLAG(FLAG_SD_OK); else CLR_FLAG(FLAG_SD_OK);
        lcdClear(); lcdSetCursor(0,0); lcdPrintP(sdOK?F("SD:OK"):F("SD:ECHEC"));
        lcdSetCursor(0,1); lcdPrintP(F("Retour ")); lcdPrintP(modeAvant==MODE_ECO?F("ECO"):F("STD"));
        etatMaint=5; tEtatMaint=now;
      }
      break;
    case 5:
      if(now-tEtatMaint>=MSG_SORTIE_MS){
        lcdClear(); lcdSetRGB({0,0,0});
        modeCourant=modeAvant;
        if(modeCourant==MODE_ECO) compteurEco=0;
        lastLogTime=now;
        CLR_FLAG(FLAG_BVERT); CLR_FLAG(FLAG_BROUGE); tVertAppui=0; tRougeAppui=0; etatMaint=0;
      }
      break;
  }
}

void entrerMaintenance(){
  modeAvant=modeCourant; modeCourant=MODE_MAINTENANCE; CLR_FLAG(FLAG_SD_OK);
  lcdSetRGB({255,100,0}); lcdClear();
  lcdSetCursor(0,0); lcdPrintP(F("MAINTENANCE"));
  lcdSetCursor(0,1); lcdPrintP(F("SD retiree OK"));
  CLR_FLAG(FLAG_BVERT); CLR_FLAG(FLAG_BROUGE); tVertAppui=0; tRougeAppui=0;
  etatMaint=1; tEtatMaint=millis();
  gAlerteModeCache = 0xFF;
}

void quitterMaintenance(){
  if(etatMaint!=3) return;
  lcdClear(); lcdSetCursor(0,0); lcdPrintP(F("Reinit SD..."));
  CLR_FLAG(FLAG_BVERT); CLR_FLAG(FLAG_BROUGE); tVertAppui=0; tRougeAppui=0;
  etatMaint=4; tEtatMaint=millis();
}

// ================= BOUTONS =================
void verifierBoutonRouge(){
  if(modeCourant==MODE_CONFIG) return;
  if(!bRougeAppuye) return;
  if(!tRougeAppui) tRougeAppui=millis();
  if(millis()-tRougeAppui>=HOLD_DURATION){
    if(digitalRead(BTN_ROUGE_PIN)==LOW)
      (modeCourant!=MODE_MAINTENANCE) ? entrerMaintenance() : quitterMaintenance();
    CLR_FLAG(FLAG_BROUGE); tRougeAppui=0;
  }
}
void verifierBoutonVert(){
  if(modeCourant==MODE_CONFIG) return;
  if(modeCourant==MODE_MAINTENANCE){ CLR_FLAG(FLAG_BVERT); tVertAppui=0; return; }
  if(!bVertAppuye) return;
  if(!tVertAppui) tVertAppui=millis();
  if(millis()-tVertAppui>=HOLD_DURATION){
    if(digitalRead(BTN_VERT_PIN)==LOW){
      modeCourant = (modeCourant==MODE_STANDARD) ? MODE_ECO : MODE_STANDARD;
      if(modeCourant==MODE_ECO) compteurEco=0;
      lastLogTime=millis();
      gAlerteModeCache = 0xFF;
    }
    CLR_FLAG(FLAG_BVERT); tVertAppui=0;
  }
}

// ================= MODES =================
void modeMaintenance(){ tickMaintenance(); }

void modeStandard(){
  static bool lec=false; gpsLireOctets();
  unsigned long now=millis();
  if(!lec&&(now-lastLogTime>=gConfig.logInterval)){ demarrerLecture(&gMesures); lec=true; }
  if(lec&&tickLecture()){ lastLogTime=millis(); lec=false; logSD(&gMesures); }
}

void modeEconomique(){
  static bool lec=false, gSess=false; gpsLireOctets();
  unsigned long now=millis();
  if(!lec&&(now-lastLogTime>=2UL*gConfig.logInterval)){
    compteurEco++; gSess=(compteurEco%2==1);
    if(!gSess) compteurEco=0;
    demarrerLecture(&gMesures); lec=true;
  }
  if(lec&&tickLecture()){
    if(!gSess) viderGPS(&gMesures);
    lastLogTime=millis(); lec=false; logSD(&gMesures);
  }
}

// ================= MODE CONFIG – TABLE DE COMMANDES =================
//
// [NOUVEAU] Chaque commande est décrite dans un struct CfgCmd.
// Le tableau kCfgCmds[] en PROGMEM remplace la chaîne de if/cmdMatch.
// cfgTraiterCommande() fait une boucle O(N) sur le tableau.
//
// Économie RAM : ~60 octets de PROGMEM pour la table vs autant de
// comparaisons câblées dans le code. Gain de lisibilité maximal :
// ajouter une commande = 1 ligne dans le tableau + 1 handler.
//
#define CFG_CMD_BUF 32
static char          cfgBuf[CFG_CMD_BUF];
static uint8_t       cfgIdx    = 0;
static unsigned long cfgLastAct  = 0;
static unsigned long cfgLastChar = 0;

static void cfgOK()  { Serial.println(F("OK")); }
static void cfgERR() { Serial.println(F("ERR")); }

static bool atoui32(const char* s, uint32_t* o){
  if(!s||!*s) return false; uint32_t v=0;
  while(*s){ if(*s<'0'||*s>'9') return false; v=v*10+(*s-'0'); s++; } *o=v; return true;
}

static void cfgParams(){
  Serial.print(F("INT="));   Serial.print(gConfig.logInterval/1000UL);
  Serial.print(F(" SZ="));   Serial.print(gConfig.fileMaxSize);
  Serial.print(F(" TO="));   Serial.print(gConfig.timeout/1000UL);
  Serial.print(F(" dDHT=")); Serial.print(erreursDHT);
  Serial.print(F(" dGPS=")); Serial.print(erreursGPS);
  Serial.print(F(" dSD="));  Serial.print(erreursSD);
  Serial.print(F(" fix="));  Serial.println(nmeaValide?1:0);
}

// --- Handlers (un par commande) ---
static void cfgHVersion(const char* arg) {
  (void)arg;
  Serial.print(F("v")); Serial.print(F(FW_VERSION));
  Serial.print(' ');    Serial.println(F(FW_LOT));
}
static void cfgHReset(const char* arg) {
  (void)arg; configDefaut(&gConfig); eepromSave(&gConfig); cfgParams(); cfgOK();
}
static void cfgHStat(const char* arg) {
  (void)arg; cfgParams();
}
static void cfgHGpsDbg(const char* arg) {
  (void)arg;
  gpsDebug = !gpsDebug;
  Serial.print(F("GPS_DEBUG=")); Serial.println(gpsDebug?1:0);
}
static void cfgHLogInt(const char* arg) {
  uint32_t v=0;
  if(atoui32(arg,&v)&&v>=1&&v<=86400){ gConfig.logInterval=v*1000UL; eepromSave(&gConfig); cfgOK(); }
  else cfgERR();
}
static void cfgHFileSize(const char* arg) {
  uint32_t v=0;
  if(atoui32(arg,&v)&&v>=512&&v<=65535UL){ gConfig.fileMaxSize=v; eepromSave(&gConfig); cfgOK(); }
  else cfgERR();
}
static void cfgHTimeout(const char* arg) {
  uint32_t v=0;
  if(atoui32(arg,&v)&&v>=1&&v<=300){ gConfig.timeout=v*1000UL; eepromSave(&gConfig); cfgOK(); }
  else cfgERR();
}
static void cfgHClock(const char* arg) {
  // arg attendu : "HH:MM:SS"
  if(arg[2]!=':'||arg[5]!=':'){ cfgERR(); return; }
  uint8_t hh=(arg[0]-'0')*10+(arg[1]-'0');
  uint8_t mi=(arg[3]-'0')*10+(arg[4]-'0');
  uint8_t ss=(arg[6]-'0')*10+(arg[7]-'0');
  if(hh<=23&&mi<=59&&ss<=59&&rtcOK){
    DateTimeRTC n=rtcNow(); rtcAdjust(n.year,n.month,n.day,hh,mi,ss); cfgOK();
  } else cfgERR();
}
static void cfgHDate(const char* arg) {
  // arg attendu : "MM,DD,YYYY"
  uint8_t c1=0, c2=0;
  for(uint8_t i=0; arg[i]; i++){
    if(arg[i]==','){ if(!c1) c1=i; else{ c2=i; break; } }
  }
  if(!c1||!c2){ cfgERR(); return; }
  char sb[5]; uint32_t mo=0, dd=0, yyyy=0; uint8_t ml;
  ml=c1; if(ml>2) ml=2;
  for(uint8_t j=0;j<ml;j++) sb[j]=arg[j]; sb[ml]='\0';
  if(!atoui32(sb,&mo)){ cfgERR(); return; }
  ml=c2-c1-1; if(ml>2) ml=2;
  for(uint8_t j=0;j<ml;j++) sb[j]=arg[c1+1+j]; sb[ml]='\0';
  if(!atoui32(sb,&dd)){ cfgERR(); return; }
  uint8_t argLen=strlen(arg); ml=argLen-c2-1; if(ml>4) ml=4;
  for(uint8_t j=0;j<ml;j++) sb[j]=arg[c2+1+j]; sb[ml]='\0';
  if(!atoui32(sb,&yyyy)){ cfgERR(); return; }
  if(mo>=1&&mo<=12&&dd>=1&&dd<=31&&yyyy>=2000&&yyyy<=2099&&rtcOK){
    DateTimeRTC n=rtcNow();
    rtcAdjust((uint16_t)yyyy,(uint8_t)mo,(uint8_t)dd,n.hour,n.minute,n.second); cfgOK();
  } else cfgERR();
}
static void cfgHDay(const char* arg) {
  // arg attendu : "MON", "TUE", ... (majuscules)
  static const char PROGMEM kJours[] = "MONTUEWEDTHUFRISATSUN";
  uint8_t dow=0;
  for(uint8_t i=0; i<7; i++){
    if(arg[0]==(char)pgm_read_byte(kJours+i*3)  &&
       arg[1]==(char)pgm_read_byte(kJours+i*3+1)&&
       arg[2]==(char)pgm_read_byte(kJours+i*3+2)){ dow=i+1; break; }
  }
  if(dow&&rtcOK){ rtcSetDOW(dow); cfgOK(); } else cfgERR();
}

// --- Chaînes préfixes en PROGMEM ---
static const char PROGMEM kCfxVersion[]  = "VERSION";
static const char PROGMEM kCfxReset[]    = "RESET";
static const char PROGMEM kCfxStat[]     = "STAT";
static const char PROGMEM kCfxGpsDbg[]   = "GPS_DEBUG";
static const char PROGMEM kCfxLogInt[]   = "LOG_INTERVAL=";
static const char PROGMEM kCfxFileSize[] = "FILE_MAX_SIZE=";
static const char PROGMEM kCfxTimeout[]  = "TIMEOUT=";
static const char PROGMEM kCfxClock[]    = "CLOCK=";
static const char PROGMEM kCfxDate[]     = "DATE=";
static const char PROGMEM kCfxDay[]      = "DAY=";

// --- Struct de dispatch ---
// pfx      : adresse PROGMEM du préfixe à matcher (insensible à la casse)
// fn       : pointeur vers le handler correspondant
// pfxLen   : longueur du préfixe (partie avant l'argument)
// totalLen : longueur totale attendue de la commande (0 = variable)
typedef void (*CfgHandlerFn)(const char*);
struct CfgCmd {
  PGM_P        pfx;
  CfgHandlerFn fn;
  uint8_t      pfxLen;
  uint8_t      totalLen;
};

static const CfgCmd PROGMEM kCfgCmds[] = {
  {kCfxVersion,  cfgHVersion,   7,  7},
  {kCfxReset,    cfgHReset,     5,  5},
  {kCfxStat,     cfgHStat,      4,  4},
  {kCfxGpsDbg,   cfgHGpsDbg,    9,  9},
  {kCfxLogInt,   cfgHLogInt,   13,  0},
  {kCfxFileSize, cfgHFileSize, 14,  0},
  {kCfxTimeout,  cfgHTimeout,   8,  0},
  {kCfxClock,    cfgHClock,     6, 14},
  {kCfxDate,     cfgHDate,      5,  0},
  {kCfxDay,      cfgHDay,       4,  7},
};
static const uint8_t kCfgCmdCount = sizeof(kCfgCmds) / sizeof(kCfgCmds[0]);

// [MODIFIÉ] Variante PROGMEM de cmdMatch
static bool cmdMatchP(const char* buf, PGM_P pfx, uint8_t n) {
  for (uint8_t i=0; i<n; i++){
    char a=buf[i]; if(a>='a'&&a<='z') a-=32;
    if(a!=(char)pgm_read_byte(pfx+i)) return false;
  }
  return true;
}

// [MODIFIÉ] Boucle sur kCfgCmds[] – plus de chaîne if/cmdMatch
static void cfgTraiterCommande(char* cmd) {
  uint8_t len=strlen(cmd);
  while(len&&(cmd[len-1]=='\r'||cmd[len-1]=='\n')) cmd[--len]='\0';
  if(!len) return;
  cfgLastAct=millis();

  CfgCmd entry;
  for (uint8_t i=0; i<kCfgCmdCount; i++){
    memcpy_P(&entry, &kCfgCmds[i], sizeof(CfgCmd));
    if(!cmdMatchP(cmd, (PGM_P)entry.pfx, entry.pfxLen)) continue;
    if(entry.totalLen && len!=entry.totalLen){ cfgERR(); return; }
    entry.fn(cmd + entry.pfxLen);
    return;
  }
  cfgERR();
}

static void cfgLireSerie(){
  while(Serial.available()){
    char c=(char)Serial.read();
    cfgLastChar=millis(); cfgLastAct=millis();
    if(c=='\n'||c=='\r'){
      if(cfgIdx>0){ cfgBuf[cfgIdx]='\0'; cfgTraiterCommande(cfgBuf); cfgIdx=0; cfgLastChar=0; }
    } else {
      if(cfgIdx<CFG_CMD_BUF-1) cfgBuf[cfgIdx++]=c;
    }
  }
  if(cfgIdx>0&&cfgLastChar>0&&(millis()-cfgLastChar>=CFG_CMD_TIMEOUT_MS)){
    cfgBuf[cfgIdx]='\0'; cfgTraiterCommande(cfgBuf); cfgIdx=0; cfgLastChar=0;
  }
}

void entrerConfig(){
  modeCourant=MODE_CONFIG; cfgIdx=0; cfgLastAct=millis(); cfgLastChar=0;
  gpsSerial.end();
  lcdSetRGB({200,200,0});
  lcdClear(); lcdSetCursor(0,0); lcdPrintP(F("MODE CONFIG")); lcdSetCursor(0,1); lcdPrintP(F("Serie 9600"));
  Serial.println(F("=CONFIG= LOG_INTERVAL= FILE_MAX_SIZE= TIMEOUT= CLOCK= DATE= DAY= VERSION RESET STAT GPS_DEBUG"));
  cfgParams();
  gAlerteModeCache = 0xFF;
}

static void quitterConfig(){
  gpsDebug=false;
  lcdClear(); lcdSetCursor(0,0); lcdPrintP(F("Config OK")); lcdSetCursor(0,1); lcdPrintP(F("Retour STD"));
  Serial.println(F("=FIN CONFIG="));
  delay(MSG_SORTIE_MS); lcdClear(); lcdSetRGB({0,0,0});
  gpsSerial.begin(GPS_BAUD);
  modeCourant=MODE_STANDARD; lastLogTime=millis();
  gAlerteModeCache = 0xFF;
  CLR_FLAG(FLAG_BVERT); CLR_FLAG(FLAG_BROUGE); tVertAppui=0; tRougeAppui=0;
}

void modeConfiguration(){
  cfgLireSerie();
  if(millis()-cfgLastAct>=CONFIG_TIMEOUT_MS) quitterConfig();
}

// ================= SETUP / LOOP =================
void setup(){
  unsigned long t=millis(); while(millis()-t<2000);

  pinMode(BTN_VERT_PIN, INPUT_PULLUP); pinMode(BTN_ROUGE_PIN, INPUT_PULLUP);
  pinMode(LED_CLK_PIN,  OUTPUT);       pinMode(LED_DATA_PIN,  OUTPUT);
  attachInterrupt(digitalPinToInterrupt(BTN_VERT_PIN),  ISR_vert,  FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_ROUGE_PIN), ISR_rouge, FALLING);

  Wire.begin(); rtcBegin(); dht.begin();
  Serial.begin(9600);
  gpsSerial.begin(GPS_BAUD);
  lcdInit(); lcdSetRGB({0,0,0}); ledCouleur({0,255,0});

  eepromLoad(&gConfig);

  t=millis(); while(millis()-t<500);
  if(SD.begin(SD_CS_PIN)) SET_FLAG(FLAG_SD_OK);

  lastLogTime=millis();

  if(digitalRead(BTN_ROUGE_PIN)==LOW) entrerConfig();
}

void loop(){
  verifierBoutonRouge();
  verifierBoutonVert();
  majAlertes(&gMesures);
  tickAlertes();

  switch(modeCourant){
    case MODE_STANDARD:    modeStandard();      break;
    case MODE_ECO:         modeEconomique();    break;
    case MODE_MAINTENANCE: modeMaintenance();   break;
    case MODE_CONFIG:      modeConfiguration(); break;
  }
}
