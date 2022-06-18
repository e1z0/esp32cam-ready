#include <esp32-hal-log.h>
#include <espcam_webserver.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <StreamString.h>
#include <time.h>

String _updaterError;

const char *updateFirmwarePage PROGMEM = "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
										 "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
										 "<input type='file' name='update'>"
										 "<input type='submit' value='Update'>"
										 "</form>"
										 "<div id='prg'>progress: 0%</div>"
										 "<script>"
										 "$('form').submit(function(e){"
										 "e.preventDefault();"
										 "var form = $('#upload_form')[0];"
										 "var data = new FormData(form);"
										 " $.ajax({"
										 "url: '/upgrade',"
										 "type: 'POST',"
										 "data: data,"
										 "contentType: false,"
										 "processData:false,"
										 "xhr: function() {"
										 "var xhr = new window.XMLHttpRequest();"
										 "xhr.upload.addEventListener('progress', function(evt) {"
										 "if (evt.lengthComputable) {"
										 "var per = evt.loaded / evt.total;"
										 "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
										 "}"
										 "}, false);"
										 "return xhr;"
										 "},"
										 "success:function(d, s) {"
										 "console.log('success!')"
										 "},"
										 "error: function (a, b, c) {"
										 "}"
										 "});"
										 "});"
										 "</script>";

String ReturnBuildTime() {
time_t rawtime = CURRENT_TIME;	
struct tm ts;
char buf[80];
ts = *localtime(&rawtime);
strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
return buf;
}

int SignalStrenght(uint8_t RSSI) {
	 if (RSSI > -55) { 
    return 100;
  } else if (RSSI < -55 & RSSI > -65) {
    return 80;
  } else if (RSSI < -65 & RSSI > -70) {
    return 60;
  } else if (RSSI < -70 & RSSI > -78) {
    return 40;
  } else if (RSSI < -78 & RSSI > -82) {
    return 20;
  } else {
    return 0;
  }

}

espcam_webserver::espcam_webserver(OV2640 &cam, const String &instance_name)
	: instance_name_(instance_name), cam_(cam), rtsp_server_(cam)
{
	// Set up required URL handlers on the web server
	server_.on("/", HTTP_GET, std::bind(&espcam_webserver::handle_root, this));
	server_.on("/reset", HTTP_GET, std::bind(&espcam_webserver::handle_reset, this));
	server_.on("/stream", HTTP_GET, std::bind(&espcam_webserver::handle_jpg_stream, this));
	server_.on("/jpg", HTTP_GET, std::bind(&espcam_webserver::handle_jpg, this));
	server_.on("/lighton", HTTP_GET, std::bind(&espcam_webserver::handle_light_on, this));
	server_.on("/lightoff", HTTP_GET, std::bind(&espcam_webserver::handle_light_off, this));
	server_.on("/lightstatus", HTTP_GET, std::bind(&espcam_webserver::handle_light_status, this));
	server_.on("/update", HTTP_GET, [&]()
			   {
      server_.sendHeader("Connection", "close");
      server_.send(200, "text/html", updateFirmwarePage); });
	server_.on("/upgrade", HTTP_POST, [&](){
      server_.sendHeader("Connection", "close");
      server_.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },[&](){
      HTTPUpload& upload = server_.upload();
      if(upload.status == UPLOAD_FILE_START){
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if(!Update.begin(UPDATE_SIZE_UNKNOWN)){//start with max available size
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_WRITE){
        /* flashing firmware to ESP*/
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    });
}

void espcam_webserver::begin()
{
	log_i("Starting rtsp_server");
	rtsp_server_.begin();


	log_i("Starting web server");
	server_.begin();

	MDNS.begin(instance_name_.c_str());
	// Add service to MmNS - http
	MDNS.addService("http", "tcp", 80);
}

void espcam_webserver::doLoop()
{
	rtsp_server_.doLoop();
	server_.handleClient();
}

void espcam_webserver::handle_root()
{
	log_i("handle_root");
	String html(
		"<!DOCTYPE html>"
		"<html lang=\"en\">"
		"<head>"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\"/>"
		"<meta http-equiv=\"Pragma\" content=\"no-cache\">"
		"<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css\">"
		"<title>"
		"ESP32 CAM"
		"</title>"
		"</head>"
		"<body>"
		"<div class=\"container\">"
		"<h2 class=\"text-center\">ESP32CAM</h2>"
		"<div class=\"alert alert-primary\" role=\"alert\">"
		"rtsp stream available at: <a class=\"alert-link\" href=\"rtsp://" +
		instance_name_ + ".local:554/mjpeg/1\">rtsp://" + instance_name_ + ".local:554/mjpeg/1</a>"
																		   "</div>"
		"<div class=\"alert alert-secondary\" role=\"alert\">"
		"CPU Mhz: "+ESP.getCpuFreqMHz()+
		" Built: "+ReturnBuildTime()+
		" Wifi Strength: "+SignalStrenght(WiFi.RSSI())+
		"%"
		"</div>"
																		   "<div class=\"list-group\">"
																		   "<button type=\"button\" class=\"list-group-item list-group-item-action active\">Options</button>"
																		   "<a class=\"list-group-item list-group-item-action\" href=\"jpg\">Single frame</a>"
																		   "<a class=\"list-group-item list-group-item-action\" href=\"stream\">Stream frames</a>"
																		   "<a class=\"list-group-item list-group-item-action\" href=\"lighton\">Light on</a>"
																		   "<a class=\"list-group-item list-group-item-action\" href=\"lightoff\">Light off</a>"
																		   "<a class=\"list-group-item list-group-item-action\" href=\"update\">Update</a>"
																		   "<a class=\"list-group-item list-group-item-action list-group-item-danger\" href=\"reset\">Reset configuration and restart</a>"
																		   "</div>"
																		   "</div>"
																		   "</body>"
																		   "</html>");

	server_.send(200, "text/html", html);
}

void espcam_webserver::handle_reset()
{
	log_i("handle_reset");
	WiFi.disconnect(false, true);
	ESP.restart();
}

void espcam_webserver::handle_jpg_stream()
{
	log_i("handle_jpg_stream");
	server_.sendContent("HTTP/1.1 200 OK\r\n"
						"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");

	auto wifi_client = server_.client();
	do
	{
		cam_.run();
		if (!wifi_client.connected())
			break;

		server_.sendContent("--frame\r\n"
							"Content-Type: image/jpeg\r\n\r\n");
		wifi_client.write(reinterpret_cast<char *>(cam_.getfb()), cam_.getSize());
		server_.sendContent("\r\n");
	} while (wifi_client.connected());
}

void espcam_webserver::handle_jpg()
{
	log_i("handle_jpg");
	auto wifi_client = server_.client();
	cam_.run();
	if (!wifi_client.connected())
		return;

	server_.sendContent("HTTP/1.1 200 OK\r\n"
						"Content-Disposition: inline; filename=capture.jpg\r\n"
						"Content-Type: image/jpeg\r\n\r\n");
	wifi_client.write(reinterpret_cast<const char *>(cam_.getfb()), cam_.getSize());
}

void espcam_webserver::handle_light_on()
{
	log_i("handle_light_on");
	digitalWrite(LED_BUILTIN, true);

	server_.sendHeader("Location", "/");
	// See Other
	server_.send(302);
}

void espcam_webserver::handle_light_off()
{
	log_i("handle_light_off");
	digitalWrite(LED_BUILTIN, false);

	server_.sendHeader("Location", "/");
	// See Other
	server_.send(302);
}

void espcam_webserver::handle_light_status()
{
	log_i("handle_light_status");
	bool on = digitalRead(LED_BUILTIN);

	server_.send(200, "text/html", on ? "1" : "0");
}
