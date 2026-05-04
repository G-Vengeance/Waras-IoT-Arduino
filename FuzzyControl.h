#include <Fuzzy.h>

Fuzzy *fuzzy = new Fuzzy();

void setupFuzzy() {
  // ==========================================
  // 1. INPUT: pH Level (0 - 14)
  // ==========================================
  FuzzyInput *phInput = new FuzzyInput(1);
  FuzzySet *phAcid = new FuzzySet(0, 0, 5, 6.5);
  FuzzySet *phNeutral = new FuzzySet(6, 6.5, 8, 8.5);
  FuzzySet *phAlkaline = new FuzzySet(8, 9, 14, 14);
  phInput->addFuzzySet(phAcid);
  phInput->addFuzzySet(phNeutral);
  phInput->addFuzzySet(phAlkaline);
  fuzzy->addFuzzyInput(phInput);

  // ==========================================
  // 2. INPUT: DO (0 - 10 mg/L)
  // ==========================================
  FuzzyInput *doInput = new FuzzyInput(2);
  FuzzySet *doLow = new FuzzySet(0, 0, 3, 5);
  FuzzySet *doMedium = new FuzzySet(4, 5, 6, 7);
  FuzzySet *doHigh = new FuzzySet(6, 7, 10, 10);
  doInput->addFuzzySet(doLow);
  doInput->addFuzzySet(doMedium);
  doInput->addFuzzySet(doHigh);
  fuzzy->addFuzzyInput(doInput);

  // ==========================================
  // 3. INPUT: Suhu (20 - 35 °C)
  // ==========================================
  FuzzyInput *suhuInput = new FuzzyInput(3);
  FuzzySet *suhuLow = new FuzzySet(20, 20, 23, 25);
  FuzzySet *suhuOptimal = new FuzzySet(24, 25, 30, 31);
  FuzzySet *suhuHigh = new FuzzySet(30, 32, 35, 35);
  suhuInput->addFuzzySet(suhuLow);
  suhuInput->addFuzzySet(suhuOptimal);
  suhuInput->addFuzzySet(suhuHigh);
  fuzzy->addFuzzyInput(suhuInput);

  // ==========================================
  // 4. OUTPUT: Feeding Rate (0 - 100%)
  // ==========================================
  FuzzyOutput *rateOutput = new FuzzyOutput(1);
  FuzzySet *rateLow = new FuzzySet(0, 0, 20, 40);
  FuzzySet *rateMedium = new FuzzySet(30, 50, 60, 80);
  FuzzySet *rateHigh = new FuzzySet(70, 85, 100, 100);
  rateOutput->addFuzzySet(rateLow);
  rateOutput->addFuzzySet(rateMedium);
  rateOutput->addFuzzySet(rateHigh);
  fuzzy->addFuzzyOutput(rateOutput);

  // ==========================================
  // 5. OUTPUT: Feeding Interval (Jam)
  // ==========================================
  FuzzyOutput *intervalOutput = new FuzzyOutput(2);
  FuzzySet *intShort = new FuzzySet(1, 1, 2, 3);
  FuzzySet *intNormal = new FuzzySet(2, 3, 4, 5);
  FuzzySet *intLong = new FuzzySet(4, 5, 6, 6);
  intervalOutput->addFuzzySet(intShort);
  intervalOutput->addFuzzySet(intNormal);
  intervalOutput->addFuzzySet(intLong);
  fuzzy->addFuzzyOutput(intervalOutput);

  // ==========================================
  // 6. MEMBUAT ATURAN (RULE BASE)
  // ==========================================
  
  // Rule 1: KONDISI SEMPURNA (pH Neutral, DO High, Suhu Optimal)
  // MAKA: Pakan High, Interval Normal
  FuzzyRuleAntecedent *kondisi1_1 = new FuzzyRuleAntecedent();
  kondisi1_1->joinWithAND(phNeutral, doHigh);
  FuzzyRuleAntecedent *kondisiSempurna = new FuzzyRuleAntecedent();
  kondisiSempurna->joinWithAND(kondisi1_1, suhuOptimal);

  FuzzyRuleConsequent *tindakanSempurna = new FuzzyRuleConsequent();
  tindakanSempurna->addOutput(rateHigh);
  tindakanSempurna->addOutput(intNormal);

  FuzzyRule *rule1 = new FuzzyRule(1, kondisiSempurna, tindakanSempurna);
  fuzzy->addFuzzyRule(rule1);

  // Rule 2: KONDISI KRITIS (DO Low ATAU Suhu High)
  // MAKA: Pakan Low, Interval Long
  FuzzyRuleAntecedent *kondisiKritis = new FuzzyRuleAntecedent();
  kondisiKritis->joinWithOR(doLow, suhuHigh);

  FuzzyRuleConsequent *tindakanKritis = new FuzzyRuleConsequent();
  tindakanKritis->addOutput(rateLow);
  tindakanKritis->addOutput(intLong);

  FuzzyRule *rule2 = new FuzzyRule(2, kondisiKritis, tindakanKritis);
  fuzzy->addFuzzyRule(rule2);
}

// ==========================================
// FUNGSI UNTUK MENDAPATKAN HASIL (Tanpa Amonia)
// ==========================================
void hitungAksiFuzzy(float ph, float doVal, float suhu, float &outRate, float &outInterval) {
  fuzzy->setInput(1, ph);
  fuzzy->setInput(2, doVal);
  fuzzy->setInput(3, suhu);

  fuzzy->fuzzify();

  outRate = fuzzy->defuzzify(1);      
  outInterval = fuzzy->defuzzify(2);  
}
