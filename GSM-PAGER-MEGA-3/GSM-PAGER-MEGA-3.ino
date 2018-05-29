/*  GSM пейджер. Креатед бай voltNik ин 2018 году нашей эры.
 *    В далеких 90х, когда у народа еще не было сотовых телефонов в ходу были пейджеры.
 *    На них можно было слать короткие сообщения через операторов. Т.е. нужно было с 
 *    городского номера набрать пейджинговую компанию, сказать оператору номер абонента 
 *    и проговорить сообщение. Весело да? Но этим пользовались! Поначалу сообщения просто 
 *    содержали обратный номер с просьбой перезвонить. Но позже начали уже отправлять длинные тексты.
 *    Пейджеры полностью умерли когда появились первые GSM телефоны с SMS.
 *    
 *    Проект сделан под модем SIM800L и _!кирилический!_ экран 2004. Декодирует и выводит на экран русские
 *    и латинские символы.
 *   
 * Полезные оманды AT
 *  AT+CPMS? -- сколько SMS в памяти
 *  AT+CMGDA="DEL ALL" -- удалить все СМС с карты
 *  AT+COPS? -- Информация об операторе
 *  AT+COPN -- список всех операторов
 *  
 *  sendATCommand("AT+CLVL?", true);          // Запрашиваем громкость динамика
 *  sendATCommand("AT+DDET=1,0,0", true);     // Включить DTMF
 *  
 *  SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10
 */
 
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <SPI.h>
#include <SD.h>
#include <TouchScreen.h>

File this_log;
MCUFRIEND_kbv tft;

#define VER "версия 2.2"
#define SD_CS 5 // пин CS подключения SD кардридера
#define file_name "sms"  // имя файлов SMS
#define BUT_PROTECT 100  // дребезг кнопки тачскрина
#define SMS_PRINT_PERIOD 30000 // Обновление чтение СМС

#define LED 33       // пин подключения светодиода - не используется
#define BUFFPIXEL 20

// подстройка резистивного тача
uint8_t YP = A1;  // must be an analog pin, use "An" notation!
uint8_t XM = A2;  // must be an analog pin, use "An" notation!
uint8_t YM = 7;   // can be a digital pin
uint8_t XP = 6;   // can be a digital pin
uint8_t SwapXY = 0;

uint16_t TS_LEFT = 880;
uint16_t TS_RT  = 170;
uint16_t TS_TOP = 180;
uint16_t TS_BOT = 950;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 260);
TSPoint tp;

#define MINPRESSURE 20
#define MAXPRESSURE 1000
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

uint16_t g_identifier, xpos, ypos;
uint8_t  spi_save;
unsigned int sms_number = 0, sms_print = 0, old_sms_number = 0, old_sms_print = 0;
long now_millis, ts_millis = 0, sms_millis = 0;
String currStr = ""; // строка сообщение
bool isStringMessage = false, hasmsg = false;
String phones = "+7928xxxxxxx, +7920xxxxxxx, +7918xxxxxxx";   // Белый список телефонов - не используется
//-----------------------------------------------------------
void setup() {
 Serial.begin(115200);
 Serial.println("MEGA GSM ПЕЙДЖЕР"); Serial.println(VER);
 Serial1.begin(115200);               // Скорость обмена данными с модемом
 
 g_identifier = tft.readID();
 Serial.print("ID = 0x");
 Serial.println(g_identifier, HEX);
 if (g_identifier == 0x00D3 || g_identifier == 0xD3D3) g_identifier = 0x9481; // write-only shield
 if (g_identifier == 0xFFFF) g_identifier = 0x9341; // serial

 tft.begin(g_identifier);
 tft.setRotation(1);
 tft.fillScreen(BLACK);
 tft.setCursor(0, 0);
 tft.setTextColor(WHITE);
 tft.setTextSize(4);
 tft.println("MEGA GSM PAGER");
 tft.setTextColor(GREEN);
 tft.println(utf8rus("      версия 2.2"));

 // настройка модема
 tft.setTextSize(1);
 tft.setCursor(0, 66);
 tft.setTextColor(YELLOW);
 tft.print(utf8rus("Инициализация модема.."));
 for (int i=1;i<5;i++) {tft.print(i); tft.print(".."); delay(1000);}
 sendATCommand("AT", true);                // Автонастройка скорости
 sendATCommand("AT+CLIP=1", true);         // Включаем АОН
 sendATCommand("AT+CMGF=1;&W", true);      // Включить TextMode для SMS
// sendATCommand("AT+IFC=1, 1", true);       // устанавливаем программный контроль потоком передачи данных
// sendATCommand("AT+CPBS=\"SM\"", true);    // открываем доступ к данным телефонной книги SIM-карты
// sendATCommand("AT+CNMI=1,2,2,1,0", true); // Включает оповещение о новых сообщениях, формат: +CMT: "<номер>", "", "<дата, время>", на следующей строчке с первого символа идёт содержимое сообщения
 
 Serial.print("Initializing SD card...");
 tft.print(utf8rus("Инициализация карты памяти.."));
  if (!SD.begin(SD_CS)) {
    tft.print(utf8rus("ОШИБКА!"));
    Serial.println("..failed!");
    while (true) {
      digitalWrite(LED, 1);
      delay(200);
      digitalWrite(LED, 0);
      delay(200);
    }
  }
  tft.println(utf8rus(" готово."));
  Serial.println(" done.");
  SD_log_init();  // выбор файла для записи и всё остальное
  delay(2000);
  //tft.fillRect(0, 32, 400, 208, BLACK);
  sms_to_tft_print(sms_number);
}
//-----------------------------------------------------------
void loop() {
  now_millis = millis();
  
  tp = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);
  pinMode(XP, OUTPUT);
  pinMode(YM, OUTPUT); 
  
  if ((tp.z > MINPRESSURE && tp.z < MAXPRESSURE) and (now_millis - ts_millis > BUT_PROTECT)) {
    ypos = map(tp.x, TS_RT, TS_LEFT, tft.height(), 0);
    xpos = map(tp.y, TS_BOT, TS_TOP, tft.width(), 0);
    Serial.print("Touch! XPOS: "); Serial.print(xpos); Serial.print(" YPOS: "); Serial.println(ypos);
    if ((xpos>0)and(xpos<200)) {
      sms_print--;
      sms_millis = now_millis;
      if (sms_print == 0) sms_print = sms_number;
      sms_to_tft_print(sms_print);
    }
    if ((xpos>200)and(xpos<400)) {
      sms_print++;
      sms_millis = now_millis;
      if (sms_print > sms_number) sms_print = 1;
      sms_to_tft_print(sms_print);
    }
    ts_millis = now_millis + 300;
  } 

  if ((sms_number != old_sms_number)or(sms_print != old_sms_print)) {
  //if (now_millis - tft_millis > TFT_RENEW) { // обновление счетчиков
    sms_counter();
    old_sms_number = sms_number;
    old_sms_print = sms_print;
    //tft_millis = now_millis;
  } 

  if ((sms_print < sms_number)and(now_millis - sms_millis > SMS_PRINT_PERIOD)) {
    sms_print++;
    sms_millis = now_millis;
    sms_to_tft_print(sms_print);
  }
}
//-----------------------------------------------------------
void serialEvent(){ // Ожидаем команды по Serial и отправляем полученную команду модему
   while (Serial.available()) {
     Serial1.write(Serial.read());
   }
}
//-----------------------------------------------------------
void serialEvent1(){
 while (Serial1.available()) {
  char currSymb = Serial1.read(); // записываем в переменую символы, которые получили от модуля.
  Serial.print(currSymb);  
  if ('\r' == currSymb) { // если получили символ перевода коректи в начало строки, это означает что передача сообщения от модуля завершена.
   if (isStringMessage) { // если текущая строка – сообщение, то обрабатываем ее
     
     int leng = currStr.length();
     if (!currStr.compareTo("on")) { // ОБРАБОТКА КОМАНД если текст сообщения совпадает с "on",
       Serial.println("LED ON!");
       digitalWrite(LED, 1); 
     } else if (!currStr.compareTo("off")) { // если текст сообщения совпадает с "off",
       Serial.println("LED OFF!");
       digitalWrite(LED, 0); 
     } else if ((currStr.startsWith("girl"))and(leng>4)and(leng<7)) { // если текст сообщения начинается с "girl"
       sms_millis = now_millis;
       sms_print--;
       old_sms_print = sms_print;
       currStr += ".bmp";
       char charBufVar[20];
       currStr.toCharArray(charBufVar, 20);
       tft.flag_write_bmp = 1;
       tft.setRotation(2);
       bmpDraw(charBufVar, 0, 0);//
       tft.flag_write_bmp = 0;
     } else {
       currStr = UCS2ToString(currStr);   // декодируем и печатаем SMS на эране
       sms_number++;   
       //sms_print = sms_number;    
       this_log = SD.open(file_name+String(sms_number) + ".txt", FILE_WRITE);  // создать файл с логом ДЛЯ ЗАПИСИ
       this_log.print(currStr);
       this_log.close();
       Serial.print("\nWriting SMS-"); Serial.print(sms_number); Serial.print(": ");Serial.print(currStr);
       //sms_to_tft_print(sms_print);
     }
    isStringMessage = false;
    } else {
      if (currStr.startsWith("+CMT")) { isStringMessage = true;} // если текущая строка начинается с "+CMT", то следующая сообщение SMS
   //   if (currStr.startsWith("RING")) { sendATCommand("ATH", false); } // Есть входящий вызов - Отклоняем вызов
    }
    currStr = ""; 
  } else if ('\n' != currSymb) { //  игнорируем второй символ в последовательности переноса строки: \r\n , и 
    currStr += String(currSymb); //дополняем текущую команду новым сиволом
  }
 }
}
//-----------------------------------------------------------
void parseSMS(String msg) {                                   // Парсинг SMS ___!!НЕ ИСПОЛЬЗУЕТСЯ!!___
  String msgheader  = "";
  String msgbody    = "";
  String msgphone   = "";

  msg = msg.substring(msg.indexOf("+CMGR: "));
  msgheader = msg.substring(0, msg.indexOf("\r"));            // Выдергиваем телефон

  msgbody = msg.substring(msgheader.length() + 2);
  msgbody = msgbody.substring(0, msgbody.lastIndexOf("OK"));  // Выдергиваем текст SMS
  msgbody.trim();

  int firstIndex = msgheader.indexOf("\",\"") + 3;
  int secondIndex = msgheader.indexOf("\",\"", firstIndex);
  msgphone = msgheader.substring(firstIndex, secondIndex);

  Serial.println("Phone: " + msgphone);                       // Выводим номер телефона
  Serial.println("Message: " + msgbody);                      // Выводим текст SMS

/*  if (msgphone.length() > 6 && phones.indexOf(msgphone) > -1) { // Если телефон в белом списке, то...
    //setLedState(msgbody, msgphone);                           // ...выполняем команду
  }
  else {
    Serial.println("Unknown phonenumber");
    }  */
}
//-----------------------------------------------------------
void sms_counter() {
  tft.setRotation(1);
  tft.fillRect(200, 219, 200, 21, RED);
  tft.setCursor(200, 219);
  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  tft.print("SMS:"); tft.print(sms_print); tft.print("-"); tft.print(sms_number);
}
//-----------------------------------------------------------
void sms_to_tft_print(int sms_num) {   
  tft.setRotation(1);
  tft.fillScreen(BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(WHITE);
  tft.setTextSize(4);
  tft.println("MEGA GSM PAGER");
  this_log = SD.open(file_name+String(sms_num) + ".txt");  // создать файл с логом ДЛЯ ЗАПИСИ
  if (this_log) {
    String msg = "";
    while (this_log.available()) {
      char symb = this_log.read();
      msg += String(symb);
    }
    this_log.close();
    Serial.println("SMS-"+(String)sms_num+": "+msg);
    tft.setCursor(0, 50);
    tft.setTextColor(GREEN);
    tft.setTextSize(4);
    tft.println(utf8rus(msg));
  } else {
    // if the file didn't open, print an error:
    tft.setCursor(0, 36);
    tft.setTextColor(RED);
    tft.setTextSize(3);
    tft.println("Error opening file SMS_"+sms_num);
    Serial.println("error opening file "+sms_num);
  }
 sms_counter();
}   
//------------------------------------------------------------
String utf8rus(String source) {
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;
  while (i < k) {
    n = source[i]; i++;
    if (n >= 0xBF){
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x2F;
          break; }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB7; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x6F;
          break; }
      }
    }
    m[0] = n; target = target + String(m);
  }
return target;
}
//-----------------------------------------------------------
String sendATCommand(String cmd, bool waiting) {
  String _resp = "";
  Serial.println(cmd);
  Serial1.println(cmd);
  tft.print(cmd); tft.print(".. ");
  if (waiting) {
    _resp = waitResponse();
    // Если Echo Mode выключен (ATE0), то эти 3 строки можно закомментировать
    if (_resp.startsWith(cmd)) {  // Убираем из ответа дублирующуюся команду
      _resp = _resp.substring(_resp.indexOf("\r", cmd.length()) + 2);
    }
    Serial.println(_resp);
    tft.print(_resp); 
  }
  return _resp;
}
//------------------------------------------------------------
String waitResponse() {                         // Функция ожидания ответа и возврата полученного результата
  String _resp = "";
  long _timeout = millis() + 5000;
  while (!Serial1.available() && millis() < _timeout)  {}; // Ждем ответа 5 секунд, если пришел ответ или наступил таймаут, то...
  if (Serial1.available()) {                     
    _resp = Serial1.readString();                
  } else {
    Serial.println("Timeout...");
  }
  return _resp;
}
//-------------------------------------------
unsigned char HexSymbolToChar(char c) {
  if      ((c >= 0x30) && (c <= 0x39)) return (c - 0x30);
  else if ((c >= 'A') && (c <= 'F'))   return (c - 'A' + 10);
  else                                 return (0);
}
//-------------------------------------------
String UCS2ToString(String decode_str) {                       // Функция декодирования UCS2 строки
  String result = "";
  unsigned char c[5] = "";                            // Массив для хранения результата

  //Serial.print("\nString0-1: "); 
  //for (int i=0; i<10; i++) {Serial.print(decode_str[i]);Serial.print("-");}
  
  if (decode_str.startsWith("0")) { // проверка на UNICODE
   //Serial.println("\nDecode!");
  for (int i = 0; i < decode_str.length() - 3; i += 4) {       // Перебираем по 4 символа кодировки
    unsigned long code = (((unsigned int)HexSymbolToChar(decode_str[i])) << 12) +    // Получаем UNICODE-код символа из HEX представления
                         (((unsigned int)HexSymbolToChar(decode_str[i + 1])) << 8) +
                         (((unsigned int)HexSymbolToChar(decode_str[i + 2])) << 4) +
                         ((unsigned int)HexSymbolToChar(decode_str[i + 3]));
    if (code <= 0x7F) {                               // Теперь в соответствии с количеством байт формируем символ
      c[0] = (char)code;                              
      c[1] = 0;                                       // Не забываем про завершающий ноль
    } else if (code <= 0x7FF) {
      c[0] = (char)(0xC0 | (code >> 6));
      c[1] = (char)(0x80 | (code & 0x3F));
      c[2] = 0;
    } else if (code <= 0xFFFF) {
      c[0] = (char)(0xE0 | (code >> 12));
      c[1] = (char)(0x80 | ((code >> 6) & 0x3F));
      c[2] = (char)(0x80 | (code & 0x3F));
      c[3] = 0;
    } else if (code <= 0x1FFFFF) {
      c[0] = (char)(0xE0 | (code >> 18));
      c[1] = (char)(0xE0 | ((code >> 12) & 0x3F));
      c[2] = (char)(0x80 | ((code >> 6) & 0x3F));
      c[3] = (char)(0x80 | (code & 0x3F));
      c[4] = 0;
    }
    result += String((char*)c);                       // Добавляем полученный символ к результату
  }
  return (result); 
  } else {
    //Serial.println("\nNo decode.");
    return (decode_str);
  }
}
//-------------------------------------------
void SD_log_init() {            // инициализация карты, поиск записанных логов
  sms_number = 1;                   // начинаем с 1 лога
  tft.print(utf8rus("Количество SMS на карте.."));
  while (true) {                // цикл поиска файлов
    // если существует
    if (SD.exists(file_name+String(sms_number) + ".txt")) {
      sms_number++;       // ищем дальше
      continue;
    } else {          // если не нашли
      sms_number--;
      sms_print = sms_number;
      tft.print(sms_number);
      Serial.println("Total SMS on SDcard: "+(String)sms_number);
      break;          // выйти из цикла поиска
    }
  }
}
//-------------------------------------------
void bmpDraw(char *filename, int x, int y) {
   File   bmpFile;
   int    bmpWidth, bmpHeight;   // W+H in pixels
   uint8_t  bmpDepth;        // Bit depth (currently must be 24)
   uint32_t bmpImageoffset;      // Start of image data in file
   uint32_t rowSize;         // Not always = bmpWidth; may have padding
   uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel in buffer (R+G+B per pixel)
   uint16_t lcdbuffer[BUFFPIXEL];  // pixel out buffer (16-bit per pixel)
   uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
   boolean  goodBmp = false;     // Set to true on valid header parse
   boolean  flip  = true;      // BMP is stored bottom-to-top
   int    w, h, row, col;
   uint8_t  r, g, b;
   uint32_t pos = 0, startTime = millis();
   uint8_t  lcdidx = 0;
   boolean  first = true;
 
   if((x >= tft.width()) || (y >= tft.height())) return;
 
   Serial.println();
   Serial.print("Loading image '");
   Serial.print(filename);
   Serial.println('\'');
   // Open requested file on SD card
   SPCR = spi_save;
   if ((bmpFile = SD.open(filename)) == NULL) {
   Serial.print("File not found");
   return;
   }
 
   // Parse BMP header
   if(read16(bmpFile) == 0x4D42) { // BMP signature
   Serial.print(F("File size: ")); Serial.println(read32(bmpFile));
   (void)read32(bmpFile); // Read & ignore creator bytes
   bmpImageoffset = read32(bmpFile); // Start of image data
   Serial.print(F("Image Offset: ")); Serial.println(bmpImageoffset, DEC);
   // Read DIB header
   Serial.print(F("Header size: ")); Serial.println(read32(bmpFile));
   bmpWidth  = read32(bmpFile);
   bmpHeight = read32(bmpFile);
   if(read16(bmpFile) == 1) { // # planes -- must be '1'
     bmpDepth = read16(bmpFile); // bits per pixel
     Serial.print(F("Bit Depth: ")); Serial.println(bmpDepth);
     if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed
 
     goodBmp = true; // Supported BMP format -- proceed!
     Serial.print(F("Image size: "));
     Serial.print(bmpWidth);
     Serial.print('x');
     Serial.println(bmpHeight);
 
     // BMP rows are padded (if needed) to 4-byte boundary
     rowSize = (bmpWidth * 3 + 3) & ~3;
 
     // If bmpHeight is negative, image is in top-down order.
     // This is not canon but has been observed in the wild.
     if(bmpHeight < 0) {
       bmpHeight = -bmpHeight;
       flip    = false;
     }
 
     // Crop area to be loaded
     w = bmpWidth;
     h = bmpHeight;
     if((x+w-1) >= tft.width())  w = tft.width()  - x;
     if((y+h-1) >= tft.height()) h = tft.height() - y;
 
     // Set TFT address window to clipped image bounds
     SPCR = 0;
     tft.setAddrWindow(x, y, x+w-1, y+h-1);
 
     for (row=0; row<h; row++) { // For each scanline...
       // Seek to start of scan line.  It might seem labor-
       // intensive to be doing this on every line, but this
       // method covers a lot of gritty details like cropping
       // and scanline padding.  Also, the seek only takes
       // place if the file position actually needs to change
       // (avoids a lot of cluster math in SD library).
       if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
       pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
       else   // Bitmap is stored top-to-bottom
       pos = bmpImageoffset + row * rowSize;
       SPCR = spi_save;
       if(bmpFile.position() != pos) { // Need seek?
       bmpFile.seek(pos);
       buffidx = sizeof(sdbuffer); // Force buffer reload
       }
 
       for (col=0; col<w; col++) { // For each column...
       // Time to read more pixel data?
       if (buffidx >= sizeof(sdbuffer)) { // Indeed
         // Push LCD buffer to the display first
         if(lcdidx > 0) {
         SPCR = 0;
         tft.pushColors(lcdbuffer, lcdidx, first);
         lcdidx = 0;
         first  = false;
         }
         SPCR = spi_save;
         bmpFile.read(sdbuffer, sizeof(sdbuffer));
         buffidx = 0; // Set index to beginning
       }
 
       // Convert pixel from BMP to TFT format
       b = sdbuffer[buffidx++];
       g = sdbuffer[buffidx++];
       r = sdbuffer[buffidx++];
       lcdbuffer[lcdidx++] = tft.color565(r,g,b);
       } // end pixel
     } // end scanline
     // Write any remaining data to LCD
     if(lcdidx > 0) {
       SPCR = 0;
       tft.pushColors(lcdbuffer, lcdidx, first);
     } 
     Serial.print(F("Loaded in "));
     Serial.print(millis() - startTime);
     Serial.println(" ms");
     } // end goodBmp
   }
   }
 
   bmpFile.close();
   if(!goodBmp) Serial.println("BMP format not recognized.");
 }
 //-------------------------------------------
 uint16_t read16(File f) {
   uint16_t result;
   ((uint8_t *)&result)[0] = f.read(); // LSB
   ((uint8_t *)&result)[1] = f.read(); // MSB
   return result;
 }
 //-------------------------------------------
 uint32_t read32(File f) {
   uint32_t result;
   ((uint8_t *)&result)[0] = f.read(); // LSB
   ((uint8_t *)&result)[1] = f.read();
   ((uint8_t *)&result)[2] = f.read();
   ((uint8_t *)&result)[3] = f.read(); // MSB
   return result;
 }
//-------------------------------------------
