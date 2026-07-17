#include "MyMesh.h"

#ifndef EWSS_CHANNEL
#define EWSS_CHANNEL 0xFF07
#endif

#ifndef EWSS_DEBUG_PRINTLN
#define EWSS_DEBUG_PRINTLN(F, ...) Serial.printf("EWSS: " F "\n", ##__VA_ARGS__)
#endif

struct EWM {
  uint8_t type : 2;

  uint16_t region : 9;

  uint8_t hazard : 7;
  uint8_t severity : 2;

  bool current_week;
  uint8_t duration : 2;

  uint8_t instrA : 5;
  uint8_t instrB : 5;

  uint8_t day : 3;
  bool morning;
  uint16_t minutes;
};

struct LanguagePack {
  std::vector<std::string> messageType;
  std::vector<std::string> regions;
  std::vector<std::string> hazardCategoryAndType;
  std::vector<std::string> severity;
  std::vector<std::string> weekNumber;
  std::vector<std::string> duration;
  std::vector<std::string> instructionA;
  std::vector<std::string> instructionB;
  std::vector<std::string> days;
  std::vector<std::string> halfDay;
};

class EWSSListener : public MyMesh {
public:
  EWSSListener(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables,
               DataStore &store, AbstractUITask *ui = NULL);

protected:
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

private:
  struct EWM parseEWSS(const uint8_t *data, size_t len);
};

extern EWSSListener the_mesh;