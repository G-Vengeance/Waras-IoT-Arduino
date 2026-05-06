#ifndef FUZZY_CONTROL_H
#define FUZZY_CONTROL_H

// ===== PARAMETER LEARNING AUTOMATA =====
float P[3] = {0.333, 0.333, 0.334}; // Probabilitas Awal: [0]Optimal, [1]Dikurangi, [2]Stop
float alpha = 0.1;                  // Learning Rate[cite: 4]
int N_ACTION = 3;

// Fungsi bantuan untuk mencari nilai maksimum dan minimum
float min3(float a, float b, float c) { return min(a, min(b, c)); }
float max3(float a, float b, float c) { return max(a, max(b, c)); }

// ==========================================
// FUNGSI UTAMA FLA (FUZZY + LA)
// ==========================================
float hitungAksiFLA(float pH, float DO, float temp) {
  
  // 1. FUZZIFIKASI PH[cite: 9]
  float acid = (pH <= 5.5) ? 1.0 : (pH < 6.5) ? (6.5 - pH) : 0.0;
  float neutral = (pH <= 6.0 || pH >= 9.0) ? 0.0 : (pH <= 7.5) ? (pH - 6.0)/1.5 : (9.0 - pH)/1.5;
  float alkaline = (pH <= 8.0) ? 0.0 : (pH < 9.0) ? (pH - 8.0) : 1.0;

  // 2. FUZZIFIKASI DO[cite: 8]
  float do_low = (DO <= 3.0) ? 1.0 : (DO < 4.0) ? (4.0 - DO) : 0.0;
  float do_medium = (DO <= 3.0 || DO >= 7.0) ? 0.0 : (DO <= 5.0) ? (DO - 3.0)/2.0 : (7.0 - DO)/2.0;
  float do_high = (DO <= 6.0) ? 0.0 : (DO < 7.0) ? (DO - 6.0) : 1.0;

  // 3. FUZZIFIKASI TEMPERATURE[cite: 1]
  float t_low = (temp <= 23.0) ? 1.0 : (temp < 25.0) ? (25.0 - temp)/2.0 : 0.0;
  float t_opt = (temp <= 24.0 || temp >= 31.0) ? 0.0 : (temp <= 27.5) ? (temp - 24.0)/3.5 : (31.0 - temp)/3.5;
  float t_high = (temp <= 30.0) ? 0.0 : (temp < 32.0) ? (temp - 30.0)/2.0 : 1.0;

  // 4. EVALUASI RULE BASE (TanPA AMONIA) || Index: 0=Optimal, 1=Dikurangi, 2=Stop
  float mu_out[3] = {0, 0, 0}; 
  
  // Rule Bahaya -> STOP (Jika DO Low, atau pH sangat buruk)
  mu_out[2] = max3(do_low, acid, alkaline);
  
  // Rule Waspada -> DIKURANGI (Jika DO Medium, atau Suhu bermasalah)
  mu_out[1] = max3(do_medium, t_low, t_high);
  
  // Rule Sempurna -> OPTIMAL (DO High, Suhu Opt, pH Neutral)
  mu_out[0] = min3(do_high, t_opt, neutral);

  // Handle Zero 
  mu_out[0] += 0.000001;
  mu_out[1] += 0.000001;
  mu_out[2] += 0.000001;

  // 5. MENGHITUNG KEPUTUSAN LA 
  float final_score[3];
  int la_action = 0;
  float max_score = -1.0;
  
  for(int i=0; i<3; i++) {
    final_score[i] = mu_out[i] * P[i]; // Perpaduan Insting Fuzzy & Probabilitas LA[cite: 3]
    if(final_score[i] > max_score) {
      max_score = final_score[i];
      la_action = i;
    }
  }

  // 6. UPDATE PROBABILITAS (REWARD & PUNISHMENT) 
  int best_action = 0;
  float max_mu = -1.0;
  for(int i=0; i<3; i++) {
    if(mu_out[i] > max_mu) {
      max_mu = mu_out[i];
      best_action = i;
    }
  }

  float reward = (la_action == best_action) ? mu_out[la_action] : 0.0;[cite: 6]

  // Eksekusi Hukuman / Hadiah pada array Probabilitas (P)
  float sum_P = 0;
  for(int j=0; j<3; j++) {
    if(reward > 0) {
      // Dikasih Reward (Aksi Tepat)
      if(j == la_action) P[j] = P[j] + alpha * reward * (1.0 - P[j]);
      else P[j] = (1.0 - alpha * reward) * P[j];
    } else {
      // Kena Penalty (Aksi Melenceng)
      if(j == la_action) P[j] = (1.0 - alpha) * P[j];
      else P[j] = (alpha / (N_ACTION - 1.0)) + (1.0 - alpha) * P[j];
    }
    sum_P += P[j];
  }
  
  // Normalisasi agar total P tetap 1.0[cite: 6]
  for(int j=0; j<3; j++) { P[j] /= sum_P; } 

  // 7. DEFUZZIFIKASI OUTPUT (Weights: Optimal=100, Red=50, Stop=0)[cite: 3]
  float weights[3] = {100.0, 50.0, 0.0};
  float crisp_output = 0;
  float sum_mu_weights = 0;
  float sum_mu = 0;
  
  for(int i=0; i<3; i++) {
    sum_mu_weights += mu_out[i] * weights[i];
    sum_mu += mu_out[i];
  }
  crisp_output = sum_mu_weights / sum_mu;[cite: 3]

  Serial.println("====== HASIL FLA (FUZZY LEARNING AUTOMATA) ======");
  Serial.printf("Crisp Pakan Dasar : %.2f%%\n", crisp_output);
  Serial.printf("Pilihan Aksi LA   : %d (0:Opt, 1:Red, 2:Stop)\n", la_action);
  Serial.printf("Reward/Penalty    : %.4f\n", reward);
  Serial.printf("Probabilitas Baru : Opt=%.3f | Red=%.3f | Stop=%.3f\n", P[0], P[1], P[2]);
  Serial.println("=================================================");

  // Modifikasi akhir output berdasarkan keputusan aksi LA
  if (la_action == 1) crisp_output *= 0.5; // Mines 50%
  if (la_action == 2) crisp_output *= 0.0; // Stop Pakan

  return crisp_output; // Mengembalikan persentase akhir (0 - 100)
}

#endif
