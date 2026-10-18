// ============================================================================
// HARNAIS DE TEST C++ AUTONOME — SOCLE MONDIAL, MARINE & PARSEUR GPS NMEA 0183
// ============================================================================
// Compilable sur PC / CI :
//   g++ -std=c++17 -Wall -Wextra -Werror -I../firmware/socle \
//       test_vigie_modele_mondial.cpp ../firmware/socle/vigie_modele_mondial.cpp \
//       -o test_vigie_modele_mondial
// ============================================================================

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "tab5_geometrie.h"
#include "vigie_modele_mondial.h"

int main() {
  // 1. Vérification des profils Multi-Écrans (Waveshare 7B 1024x600 & M5Stack Tab5 1280x720)
  const VigieProfilCarte& p7b = vigie_profil_carte("waveshare-7b");
  assert(p7b.carte == VigieCarteCible::WAVESHARE_7B);
  assert(p7b.ecran_l == 1024);
  assert(p7b.ecran_h == 600);
  assert(p7b.police_pct == 82);
  assert(p7b.gps_uart_rx_defaut == 4);
  assert(p7b.gps_uart_tx_defaut == 5);

  const VigieProfilCarte& ptab5 = vigie_profil_carte("tab5");
  assert(ptab5.carte == VigieCarteCible::M5STACK_TAB5);
  assert(ptab5.ecran_l == 1280);
  assert(ptab5.ecran_h == 720);
  assert(ptab5.police_pct == 100);
  assert(ptab5.a_imu == true);
  assert(ptab5.a_jauge_ina226 == true);
  assert(ptab5.gps_uart_rx_defaut == 53);
  assert(ptab5.gps_uart_tx_defaut == 54);

  // 2. Initialisation de l'état mondial
  VigieEtatMondial etat;
  vigie_initialiser_etat_mondial(etat);
  assert(!etat.gps.valide);
  assert(etat.nb_alertes == 0);

  // 3. Test du checksum et du parseur NMEA 0183 ($GNRMC et $GNGGA)
  assert(vigie_verifier_checksum_nmea("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59"));
  assert(!vigie_verifier_checksum_nmea("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00"));

  // Décodage GGA valide (48°51.39'N, 002°21.13'E -> 48.8565°N, 2.352167°E)
  const char* gga_paris = "$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B";
  assert(vigie_parse_nmea_sentence(gga_paris, etat.gps));
  assert(etat.gps.valide);
  assert(etat.gps.satellites == 11);
  assert(std::fabs(etat.gps.hdop - 0.8f) < 0.01f);
  assert(std::fabs(etat.gps.altitude_m - 35.2f) < 0.05f);
  assert(std::fabs(etat.gps.lat_deg - 48.8565) < 0.0005);
  assert(std::fabs(etat.gps.lon_deg - 2.352167) < 0.0005);

  char coord_buf[32] = {};
  assert(etat.gps.formater_coord_nautique(coord_buf, sizeof(coord_buf)));
  assert(std::strcmp(coord_buf, "48°51.39'N 002°21.13'E") == 0);

  // Décodage RMC valide (déplacement du navire au large de Brest : 48°23.40'N, 004°29.16'W)
  const char* rmc_brest = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C";
  assert(vigie_parse_nmea_sentence(rmc_brest, etat.gps));
  assert(etat.gps.valide);
  assert(std::fabs(etat.gps.lat_deg - 48.39) < 0.0005);
  assert(std::fabs(etat.gps.lon_deg - (-4.486)) < 0.0005);
  assert(std::fabs(etat.gps.vitesse_noeuds - 14.2f) < 0.05f);
  assert(std::fabs(vigie_noeuds_vers_kmh(etat.gps.vitesse_noeuds) - (14.2f * 1.852f)) < 0.1f);
  assert(std::fabs(etat.gps.cap_deg - 270.0f) < 0.05f);
  assert(etat.gps.formater_coord_nautique(coord_buf, sizeof(coord_buf)));
  assert(std::strcmp(coord_buf, "48°23.40'N 004°29.16'W") == 0);

  // 4. Géométrie Haversine, Azimut et Recalcul Temps Réel des Alertes
  etat.nb_alertes = 1;
  VigieAlerteCatastrophe& alerte_acores = etat.alertes[0];
  std::strncpy(alerte_acores.id, "USGS-ACORES-01", sizeof(alerte_acores.id) - 1);
  alerte_acores.categorie = VigieCategorieAlerte::TSUNAMI;
  alerte_acores.severite = VigieSeverite::ROUGE;
  alerte_acores.lat = 38.5;
  alerte_acores.lon = -28.0;
  alerte_acores.valeur_principale = 7.4f;
  alerte_acores.alerte_tsunami = true;

  vigie_recalculer_distances_gps(etat);
  vigie_recalculer_compteurs_alertes(etat);
  assert(etat.nb_alertes_rouges == 1);

  float dist1 = etat.alertes[0].distance_km;
  assert(dist1 > 2000.0f && dist1 < 2300.0f);
  assert(std::strcmp(vigie_point_cardinal(etat.alertes[0].azimut_deg), "OSO") == 0);

  // Le navire se rapproche des Açores (40°00.00'N, 025°00.00'W)
  const char* gga_acores = "$GPGGA,180000.00,4000.0000,N,02500.0000,W,1,09,1.1,0.0,M,0.0,M,,*4D";
  assert(vigie_parse_nmea_sentence(gga_acores, etat.gps));
  vigie_recalculer_distances_gps(etat);
  float dist2 = etat.alertes[0].distance_km;
  assert(dist2 < 350.0f && dist2 < dist1);

  std::printf("OK: Tous les tests C++ de vigie_modele_mondial ont reussi.\n");
  return 0;
}
