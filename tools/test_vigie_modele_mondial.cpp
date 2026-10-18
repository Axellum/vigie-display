// ============================================================================
// HARNAIS DE TEST C++ AUTONOME — SOCLE MONDIAL, MARINE, GPS, API, CARTE & NVS
// ============================================================================
// Compilable sur PC / CI :
//   g++ -std=c++17 -Wall -Wextra -Werror -I../firmware/socle -I../firmware/ecran \
//       test_vigie_modele_mondial.cpp ../firmware/socle/vigie_modele_mondial.cpp \
//       ../firmware/ecran/vigie_client_api.cpp -o test_vigie_modele_mondial
// ============================================================================

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "tab5_geometrie.h"
#include "vigie_client_api.h"
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

  const char* rmc_brest = "$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C";
  assert(vigie_parse_nmea_sentence(rmc_brest, etat.gps));
  assert(etat.gps.valide);
  assert(std::fabs(etat.gps.lat_deg - 48.39) < 0.0005);
  assert(std::fabs(etat.gps.lon_deg - (-4.486)) < 0.0005);
  assert(std::fabs(etat.gps.vitesse_noeuds - 14.2f) < 0.05f);
  assert(etat.gps.formater_coord_nautique(coord_buf, sizeof(coord_buf)));
  assert(std::strcmp(coord_buf, "48°23.40'N 004°29.16'W") == 0);

  // 4. Test du décodeur de trame API (HEAD, ZONE, RAIN5M, DAY, HAZ) + Client API
  const char* trame_api =
      "HEAD|1792332000|6.3|0\n"
      "ZONE|GPS|Golfe de Gascogne|48.0|-4.0|7200|18.4|16.9|82|1004.8|45.4|63.0|255|95|4.2|24|15|8.2|0.5,3.2,7.8,2.1|3.4|11.5|280|2.9|1.8\n"
      "RAIN5M|GPS|0.0,0.2,0.8,2.4,5.6,8.2,6.4,3.8,1.9,0.6,0.1,0.0\n"
      "DAY|GPS|2026-10-19|61|13.2|19.1|14.5|85|58.0|4.0\n"
      "HAZ|IMO-IS-2026|VOLCAN|ORANGE|63.88|-22.43|4.0|5.0|0|1792331000|IMO|Eruption Reykjanes\n"
      "HAZ|USGS-ACORES-01|TSUNAMI|ROUGE|38.5|-28.0|7.4|18.0|1|1792331500|USGS,PTWC|Seisme M7.4 Acores\n";

  VigieStatutClientApi statut_api{};
  VigieDecisionUrgence urgence{};
  assert(vigie_client_api_recevoir_trame(trame_api, etat, statut_api, 10000, urgence));
  assert(statut_api.en_ligne);
  assert(etat.nb_zones == 1);
  assert(etat.zones[0].est_zone_gps_mobile);
  // La Zone 0 GPS mobile a été synchronisée avec le fix GPS de Brest (48.39, -4.486)
  assert(std::fabs(etat.zones[0].lat - 48.39) < 0.001);
  assert(std::fabs(etat.zones[0].marin.hauteur_vagues_m - 3.4f) < 0.01f);
  assert(etat.zones[0].nb_jours == 1);
  assert(etat.nb_alertes == 2);
  // Trié par gravité : ROUGE (USGS-ACORES-01) en premier !
  assert(std::strcmp(etat.alertes[0].id, "USGS-ACORES-01") == 0);
  assert(etat.nb_alertes_rouges == 1);
  assert(etat.nb_alertes_oranges == 1);
  assert(urgence.declencher && urgence.reveiller_dalle && urgence.alerte_sonore);
  assert(std::strcmp(urgence.alerte_id, "USGS-ACORES-01") == 0);

  // 5. Acquittement d'alerte (anti-répétition)
  assert(vigie_acquitter_alerte(etat, "USGS-ACORES-01"));
  assert(vigie_alerte_est_acquittee(etat, "USGS-ACORES-01"));
  VigieDecisionUrgence urgence_apres = vigie_evaluer_declenchement_alerte(etat, 1500.0f);
  assert(!urgence_apres.declencher);  // L'alerte ORANGE d'Islande est à > 1500 km de Brest

  // 6. Projection cartographique Multi-Écrans (Waveshare 7B 1024x600 vs Tab5 1280x720)
  VigieCadreCarte c7b = vigie_cadre_carte_ecran("waveshare-7b");
  VigieCadreCarte ctab5 = vigie_cadre_carte_ecran("tab5");
  assert(c7b.largeur == 640 && c7b.hauteur == 360);
  assert(ctab5.largeur == 800 && ctab5.hauteur == 432);
  VigiePointPixel eq7b = vigie_projeter_coord_carte(0.0, 0.0, c7b);
  assert(eq7b.dans_cadre && eq7b.x == c7b.x + 320 && eq7b.y == c7b.y + 180);

  // 7. Persistance Binaire NVS avec CRC32
  VigieBlocNvs bloc{};
  vigie_preparer_bloc_nvs(etat, bloc);
  assert(bloc.magic == kVigieNvsMagic);
  VigieEtatMondial etat_restaure{};
  assert(vigie_restaurer_bloc_nvs(bloc, etat_restaure));
  assert(etat_restaure.nb_alertes == 2);
  assert(std::strcmp(etat_restaure.alertes[0].id, "USGS-ACORES-01") == 0);
  // Corruption du CRC32 -> rejet garanti
  bloc.crc32 ^= 0x00000001UL;
  assert(!vigie_restaurer_bloc_nvs(bloc, etat_restaure));

  // 8. Construction d'URL dynamique Client API (avec et sans GPS)
  char url_buf[256] = {};
  assert(vigie_construire_url_api("https://api.vigie.world/", "waveshare-7b", etat.gps, url_buf, sizeof(url_buf)));
  assert(std::strstr(url_buf, "device_type=waveshare-7b") != nullptr);
  assert(std::strstr(url_buf, "marine=1") != nullptr);

  std::printf("OK: Tous les tests C++ de vigie_modele_mondial (Vague 2) ont reussi.\n");
  return 0;
}
