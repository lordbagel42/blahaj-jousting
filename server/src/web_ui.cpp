// server/src/web_ui.cpp
#include "web_ui.h"
#include "web_ui_html.h"
#include <ArduinoJson.h>
#include <Arduino.h>

WebUI::WebUI(PairingManager& pairing, GameEngine& engine)
    : _pairing(pairing), _engine(engine) {}

void WebUI::begin() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", UI_HTML);
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildStatusJson(millis()));
    });

    _server.on("/api/start", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _pending_start = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/end", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _pending_end = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // /api/pair accepts JSON array: [{car: 0, client: 0}, {car: 1, client: 1}, ...]
    _server.on("/api/pair", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) {
                body = "";
            }
            body += String((char*)data, len);
            if (index + len >= total) {
                JsonDocument doc;
                std::vector<PairAssignment> local_pairs;
                if (deserializeJson(doc, body) == DeserializationError::Ok) {
                    for (JsonObject obj : doc.as<JsonArray>()) {
                        PairAssignment pa;
                        pa.car_idx    = obj["car"].as<uint8_t>();
                        pa.client_idx = obj["client"].as<uint8_t>();
                        local_pairs.push_back(pa);
                    }
                }
                taskENTER_CRITICAL(&_pair_mux);
                _pending_pairs = std::move(local_pairs);
                _pending_pair = true;
                taskEXIT_CRITICAL(&_pair_mux);
                req->send(200, "application/json", "{\"ok\":true}");
            }
        }
    );

    _server.begin();
}

String WebUI::buildStatusJson(uint32_t now_ms) {
    const auto& cars    = _pairing.cars();
    const auto& clients = _pairing.clients();
    const auto& ctx     = _engine.context();

    String json = "{";
    json += "\"game_state\":" + String((uint8_t)ctx.state) + ",";
    json += "\"all_paired\":" + String(_pairing.allPaired() ? "true" : "false") + ",";

    json += "\"round_wins\":[";
    for (int i = 0; i < 3; i++) {
        if (i) json += ",";
        json += String(ctx.round_wins[i]);
    }
    json += "],";

    json += "\"cars\":[";
    for (size_t i = 0; i < cars.size(); i++) {
        if (i) json += ",";
        const auto& c = cars[i];
        bool connected = (now_ms - c.last_seen_ms) < 3000;
        json += "{\"device_id\":" + String(c.device_id);
        json += ",\"type\":\"CAR\"";
        json += ",\"mac\":[";
        for (int m = 0; m < 6; m++) { if(m) json += ","; json += String(c.mac[m]); }
        json += "]";
        json += ",\"paired\":" + String(c.paired ? "true" : "false");
        json += ",\"partner_slot\":" + String(c.partner_slot);
        json += ",\"connected\":" + String(connected ? "true" : "false");
        json += "}";
    }
    json += "],";

    json += "\"clients\":[";
    for (size_t i = 0; i < clients.size(); i++) {
        if (i) json += ",";
        const auto& c = clients[i];
        bool connected = (now_ms - c.last_seen_ms) < 3000;
        json += "{\"device_id\":" + String(c.device_id);
        json += ",\"type\":\"CLIENT\"";
        json += ",\"mac\":[";
        for (int m = 0; m < 6; m++) { if(m) json += ","; json += String(c.mac[m]); }
        json += "]";
        json += ",\"paired\":" + String(c.paired ? "true" : "false");
        json += ",\"partner_slot\":" + String(c.partner_slot);
        json += ",\"connected\":" + String(connected ? "true" : "false");
        json += "}";
    }
    json += "]}";

    return json;
}
