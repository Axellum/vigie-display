/**
 * [AI-CONTEXT]
 * @file tab5_batterie.cpp
 * @role Batterie de la tablette M5Stack Tab5 (08/10/2026) : l'état vivant (présence,
 *       chargeur, alerte) et les appels du YAML pour la carte Tab5 (INA226 2S).
 *       Les règles sont dans tab5_batterie.h (pures, testées sur PC).
 * @architecture_constraint Ni LVGL ni entité Home Assistant ici : 100 % local.
 */
#include "tab5_batterie.h"

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

static const char* const TAG = "tab5.batterie";

static EtatChargeur s_chargeur;
static bool s_demarre = false;  // chargeur_demarrer() appelé (premier pas)
static EtatAlerteBatterie s_alerte;
static float s_puissance_w = NAN;

// Verrou thermique gardé en NVS (16/10/2026) : 1 = posé. Écrit seulement quand il change.
static constexpr uint32_t kPrefVerrouThermique = 0x63687468;  // « chth »
static esphome::ESPPreferenceObject s_pref_thermique;
static bool s_pref_thermique_ok = false;
static bool s_thermique_garde = false;  // valeur en NVS
static bool s_memoire_a_ecrire = false;

static bool verrou_thermique_lire() {
    s_pref_thermique = esphome::global_preferences->make_preference<uint8_t>(kPrefVerrouThermique);
    s_pref_thermique_ok = true;
    uint8_t v = 0;
    s_thermique_garde = s_pref_thermique.load(&v) && v == 1;
    return s_thermique_garde;
}

static void verrou_thermique_garder() {
    if (!s_pref_thermique_ok || s_chargeur.verrou_thermique == s_thermique_garde) return;
    s_thermique_garde = s_chargeur.verrou_thermique;
    const uint8_t v = s_thermique_garde ? 1 : 0;
    s_pref_thermique.save(&v);
    esphome::global_preferences->sync();
    ESP_LOGI(TAG, "Verrou thermique %s (garde en NVS)", s_thermique_garde ? "pose" : "leve");
}

static void journal_declenchement(CauseSecurite avant, float mesure) {
    if (avant != CauseSecurite::AUCUNE || s_chargeur.securite_sonde == CauseSecurite::AUCUNE) return;
    if (s_chargeur.securite_sonde == CauseSecurite::TENSION)
        ESP_LOGE(TAG, "SECURITE : %.2f V chargeur allume (plus qu'une 2S pleine) : chargeur coupe, sonde", mesure);
    else
        ESP_LOGE(TAG, "SECURITE : aucun courant de charge (%.3f A) depuis 30 s : chargeur coupe, sonde", mesure);
}

bool chargeur_tension(float tension, uint32_t maintenant_ms) {
    const CauseSecurite avant = s_chargeur.securite_sonde;
    const bool change = chargeur_lecture(s_chargeur, tension, maintenant_ms);
    journal_declenchement(avant, tension);
    return change;
}

void chargeur_courant(float courant_a, uint32_t maintenant_ms) {
    const CauseSecurite avant = s_chargeur.securite_sonde;
    chargeur_lecture_courant(s_chargeur, courant_a, maintenant_ms);
    journal_declenchement(avant, courant_a);
}

ActionChargeur chargeur_pas(uint8_t limite, bool montee, uint8_t memoire, bool anormal, uint32_t maintenant_ms) {
    if (s_memoire_a_ecrire) {
        s_memoire_a_ecrire = false;
        esphome::global_preferences->sync();
    }
    if (!s_demarre) {
        s_demarre = true;
        const bool thermique = verrou_thermique_lire();
        chargeur_demarrer(s_chargeur, memoire, montee, anormal, thermique, maintenant_ms);
        const char* vu = "inconnu";
        if (memoire == static_cast<uint8_t>(MemoireBatterie::AUCUNE)) vu = "aucune batterie";
        if (memoire == static_cast<uint8_t>(MemoireBatterie::VUE)) vu = "une batterie";
        const char* suite = "pas de reveil";
        if (s_chargeur.reveil_apres_sonde) suite = "reveil 30 s si rien n'est vu";
        if (thermique) suite = "verrou thermique garde";
        ESP_LOGI(TAG, "Demarrage precedent : %s%s ; chargeur coupe, sonde d'abord (%s)", vu,
                 anormal ? " (redemarrage anormal)" : "", suite);
    }
    const bool avant = s_chargeur.allume;
    const bool sonde_avant = s_chargeur.sonde;
    const CauseSecurite cause_avant = s_chargeur.securite_sonde;
    const bool verrou_avant = s_chargeur.verrou_electrique;
    const ActionChargeur a = chargeur_tick(s_chargeur, limite, montee, maintenant_ms);
    verrou_thermique_garder();
    if (s_chargeur.verrou_electrique && !verrou_avant) {
        ESP_LOGE(TAG, "Chargeur VERROUILLE jusqu'au redemarrage : 2 coupures de securite en moins d'une heure");
    }
    if (a.allumer != avant) {
        const char* raison;
        if (s_chargeur.verrou_thermique) raison = "securite thermique (CPU trop chaud)";
        else if (s_chargeur.verrou_electrique) raison = "verrou electrique (jusqu'au redemarrage)";
        else if (cause_avant != CauseSecurite::AUCUNE) raison = "securite electrique";
        else if (s_chargeur.sonde && !sonde_avant) raison = "sonde : tension lue chargeur coupe";
        else if (s_chargeur.reveil) raison = "reveil d'une batterie (demarrage ou batterie montee)";
        else if (s_chargeur.presence == PresenceBatterie::ABSENTE) raison = "pas de batterie";
        else if (s_chargeur.presence == PresenceBatterie::INCONNUE) raison = "batterie inconnue";
        else if (static_cast<LimiteCharge>(limite) == LimiteCharge::QUATRE_VINGTS) raison = "limite 80 %";
        else raison = "batterie";
        ESP_LOGI(TAG, "Chargeur %s (%s, niveau %.0f %%)", a.allumer ? "allume" : "coupe", raison,
                 s_chargeur.niveau);
    }
    return a;
}

uint8_t chargeur_memoire(uint8_t memoire) {
    const uint8_t m = batterie_memoire_suivante(s_chargeur, memoire);
    if (m != memoire) {
        s_memoire_a_ecrire = true;
        ESP_LOGI(TAG, "Memorise pour le demarrage suivant : %s",
                 m == static_cast<uint8_t>(MemoireBatterie::VUE) ? "batterie vue" : "aucune batterie");
    }
    return m;
}

bool chargeur_sonde_en_cours(uint32_t maintenant_ms) {
    return chargeur_en_sonde(s_chargeur, maintenant_ms);
}

PresenceBatterie batterie_presence() { return s_chargeur.presence; }

bool batterie_presente() { return s_chargeur.presence == PresenceBatterie::PRESENTE; }

float batterie_derniere_tension() { return s_chargeur.tension; }

float batterie_niveau_de(float tension) {
    return batterie_presente() ? batterie_niveau_pct(tension) : NAN;
}

float batterie_consommation(float courant_a, bool sur_batterie) {
    s_puissance_w = batterie_puissance_w(s_chargeur.tension, courant_a, sur_batterie);
    return s_puissance_w;
}

float batterie_derniere_consommation() { return s_puissance_w; }

int batterie_alerte(float niveau, bool sur_batterie) {
    const int seuil = batterie_alerte_lue(s_alerte, niveau, sur_batterie);
    if (seuil > 0) ESP_LOGI(TAG, "Batterie faible : %.0f %% (seuil %d %%)", niveau, seuil);
    return seuil;
}

bool chargeur_thermique_releve(float temp_c, uint32_t maintenant_ms) {
    const bool alerte = chargeur_surveillance_thermique(s_chargeur, temp_c, maintenant_ms);
    if (alerte) {
        ESP_LOGE(TAG, "URGENCE THERMIQUE : CPU à %.1f °C ! Chargeur immédiatement verrouillé et coupé", temp_c);
    }
    return alerte;
}

bool chargeur_securite_active() { return chargeur_securite_posee(s_chargeur); }

SignalSecurite chargeur_securite_signal() { return chargeur_signal_prendre(s_chargeur); }

void chargeur_rendu_coupe(uint32_t maintenant_ms) {
    s_demarre = true;
    s_chargeur.reveil = false;
    s_chargeur.sonde = false;
    s_chargeur.allume = false;
    s_chargeur.change_ms = maintenant_ms - kChargeurReposMs;
}
