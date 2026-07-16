#include "MyMesh.h"

#ifndef EWSS_CHANNEL
#define EWSS_CHANNEL 0xFF07
#endif

#ifndef EWSS_DEBUG_PRINTLN
#define EWSS_DEBUG_PRINTLN(F, ...) Serial.printf("EWSS: " F "\n", ##__VA_ARGS__)
#endif

class EWSSListener : public MyMesh {
public:
  EWSSListener(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables,
               DataStore &store, AbstractUITask *ui = NULL);

protected:
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

private:
  void handleEWSS(const uint8_t *data, size_t len);
};

extern EWSSListener the_mesh;