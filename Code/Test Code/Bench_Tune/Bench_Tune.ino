// Bench_Tune — drive the machine over Serial and get every encoder reading
// back, so motion parameters can be tuned from a script instead of by hand.
//
// WHAT IT IS FOR
// --------------
// The firmware's moves are open loop with an encoder check afterwards, and
// the only evidence of a bad move today is a jam. This sketch runs the SAME
// library code (CubeMotors, CubeSystem::checkAlignment/homeMotors) but
// reports, for every move, where each motor's encoder stood before, where it
// stood after, how far that is from where the commanded steps should have put
// it, and which calibration mark it is nearest. Run a few hundred moves at a
// setting and the residual distribution says whether that setting loses
// steps. Change one parameter and run again.
//
// It is headless — begin(kNoDisplay) — so the panel never competes with a
// step loop for time. The abort chord still works (the pump is registered),
// and any byte arriving on Serial during a repeat run stops it at the next
// move boundary.
//
// PROTOCOL (line oriented, ASCII, for a script to parse)
// ------------------------------------------------------
// Every command is answered by zero or more TAGGED data lines and then exactly
// one terminator: `ok` or `err <n> <text>`. Lines starting with `#` are for a
// human; lines starting with `[` are the library's own diagnostics
// ([align] ...). Motor order everywhere is U R F D L B (RING seventh).
//
//   help                      this list
//   status                    status hold= en= ring= top= bot= calibrated= boothome= align=
//   get                       param speed= accel= delay= rotdelay= dwell= tol= hometimeout= turnstep= hold= align=
//   set <name> <value>        names as in `get`; ranges are the library's own clamps
//   enc                       enc U=<raw> ... RING=<raw>   (negative = I2C error)
//                             near U=<idx>:<err> ...      (nearest mark, signed error)
//   cal                       cal <M> <m0> <m1> <m2> <m3> gaps <g01> <g12> <g23> <g30>
//   noise <motor> <n>         noise <M> n= min= max= mean=   (n back-to-back reads)
//   relax                     relax U=<energised>/<relaxed>/<delta> ...  (needs hold off)
//   hold on|off               keep all face motors energised between moves
//   align on|off              run checkAlignment()+homeMotors() after each move (default on)
//   load | unload             the real clamp / release sequences
//   servo top|bot ext|ret|partial|eject
//   ring ext|ret|mid|partial
//   home                      home code=<c> ms=<t>  then enc/near lines
//   move <tok>                one move; prints a mv record (below)
//   seq <tok> <tok> ...       each move in turn; stops at the first failure
//   rep <n> <tok> ...         the sequence n times; stops on failure or any Serial byte;
//                             prints a rep summary
//   stop                      holdEnd() + disableMotors()
//
// The move record:
//   mv <tok> code=<c> aligned=<0|1> homed=<-|code> ms=<move_ms>
//      U=<before>,<after>,<res>,<idx>,<err> R=... F=... D=... L=... B=...
//   before/after  raw encoder counts around the physical move
//   res           after minus where the commanded steps should have put it,
//                 shortest way round, in counts (10.24 per step). A residual
//                 near +-2048 on a quarter turn means the sign convention
//                 (CubeSystem::kAlignStepSign) is wrong, not the motor.
//   idx, err      nearest calibration mark after the move and the signed
//                 error to it
//   ms            the physical move including its settle; the alignment
//                 check and any homing are NOT in it
//
// Move tokens are exactly CubeMotors::executeMove()'s: U U' U2 ... B2, plus
// ROTX ROTZ ALL.

#include <CubeSystem.h>
#include <CubePump.h>

CubeSystem Cube;                    // CubeTuneTable.cpp links against this name

static const int  kMotors = 6;
static const char* const kNames[7] = {"U", "R", "F", "D", "L", "B", "RING"};

static bool alignMode = true;
static char lineBuf[160];
static int  lineLen = 0;

// ---------------------------------------------------------------- helpers --

static void ok()                          { Serial.println(F("ok")); }
static void err(int n, const char* text)  { Serial.print(F("err ")); Serial.print(n); Serial.print(' '); Serial.println(text); }

static bool validToken(const char* t) {
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 3; ++k)
            if (strcmp(t, CubeSystem::kFaceMoves[f][k]) == 0) return true;
    return strcmp(t, "ROTX") == 0 || strcmp(t, "ROTZ") == 0 || strcmp(t, "ALL") == 0;
}

static void readAll(int out[7]) {
    for (int i = 0; i < 7; ++i) out[i] = MotorEncoders[i]->scanChecked();
}

// Nearest calibration mark and signed error to it, the aligner's own test.
static int nearestMark(int motor, int raw, int* errOut) {
    int best = 0, bestErr = 4096;
    for (int j = 0; j < 4; ++j) {
        int e = CubeSystem::encError(raw, MotorEncoders[motor]->getCalibration(j));
        if (abs(e) < abs(bestErr)) { bestErr = e; best = j; }
    }
    if (errOut) *errOut = bestErr;
    return best;
}

static void printEnc(const int v[7]) {
    Serial.print(F("enc"));
    for (int i = 0; i < 7; ++i) { Serial.print(' '); Serial.print(kNames[i]); Serial.print('='); Serial.print(v[i]); }
    Serial.println();
    if (!Cube.getMotorCalibration()) return;
    Serial.print(F("near"));
    for (int i = 0; i < kMotors; ++i) {
        Serial.print(' '); Serial.print(kNames[i]); Serial.print('=');
        if (v[i] < 0) { Serial.print(F("?:?")); continue; }
        int e; int k = nearestMark(i, v[i], &e);
        Serial.print(k); Serial.print(':'); Serial.print(e);
    }
    Serial.println();
}

static void cmdEnc() {
    int v[7]; readAll(v); printEnc(v); ok();
}

static void cmdStatus() {
    Serial.print(F("status hold=")); Serial.print(cubeMotors.isHeld() ? 1 : 0);
    Serial.print(F(" ring="));       Serial.print(cubeMotors.getRingState());
    Serial.print(F(" top="));        Serial.print(topServo.coarseState());
    Serial.print(F(" bot="));        Serial.print(botServo.coarseState());
    Serial.print(F(" calibrated=")); Serial.print(Cube.getMotorCalibration() ? 1 : 0);
    Serial.print(F(" boothome="));   Serial.print(Cube.motorHomeState);
    Serial.print(F(" align="));      Serial.print(alignMode ? 1 : 0);
    Serial.print(F(" encmux="));     Serial.print(Cube.encoderMuxOk ? 1 : 0);
    Serial.print(F(" encoders="));
    for (int i = 0; i < 7; ++i) Serial.print(Cube.motorEncoderOk[i] ? '1' : '0');
    Serial.println();
    ok();
}

static void cmdGet() {
    Serial.print(F("param speed="));  Serial.print(cubeMotors.getStepSpeed());
    Serial.print(F(" accel="));       Serial.print(cubeMotors.getStepAccel());
    Serial.print(F(" delay="));       Serial.print(cubeMotors.getStepDelay());
    Serial.print(F(" rotdelay="));    Serial.print(cubeMotors.getRotStepDelay());
    Serial.print(F(" dwell="));       Serial.print(cubeMotors.getEnableDwell());
    Serial.print(F(" tol="));         Serial.print(Cube.motorAlignmentTol);
    Serial.print(F(" hometimeout=")); Serial.print(Cube.homeTimeout);
    Serial.print(F(" turnstep="));    Serial.print(cubeMotors.getTurnStep());
    Serial.print(F(" hold="));        Serial.print(cubeMotors.isHeld() ? 1 : 0);
    Serial.print(F(" align="));       Serial.print(alignMode ? 1 : 0);
    Serial.println();
    ok();
}

static void cmdSet(const char* name, const char* value) {
    if (!name || !value) { err(2, "set <name> <value>"); return; }
    long v = atol(value);
    if      (!strcmp(name, "speed"))       cubeMotors.setStepSpeed((int)v);
    else if (!strcmp(name, "accel"))       cubeMotors.setStepAccel((int)v);
    else if (!strcmp(name, "delay"))       cubeMotors.setStepDelay((int)v);
    else if (!strcmp(name, "rotdelay"))    cubeMotors.setRotStepDelay((int)v);
    else if (!strcmp(name, "dwell"))       cubeMotors.setEnableDwell((int)v);
    // Tolerance floor: half a step is 5.12 counts, so below ~8 the step
    // lattice may hold no point inside the band and the aligner hunts to its
    // timeout. Ceiling 60 (~5 deg) — the firmware's own table allows 200,
    // which is the "accepts a 10-degree error" case the review flagged.
    else if (!strcmp(name, "tol"))         Cube.motorAlignmentTol = (int)constrain(v, 8, 60);
    else if (!strcmp(name, "hometimeout")) Cube.homeTimeout = (unsigned long)constrain(v, 200, 10000);
    else if (!strcmp(name, "hold"))        { if (v) cubeMotors.holdBegin(); else cubeMotors.holdEnd(); }
    else if (!strcmp(name, "align"))       alignMode = (v != 0);
    else { err(3, "unknown parameter"); return; }
    cmdGet();
}

static void cmdCal() {
    if (!Cube.getMotorCalibration()) { err(4, "motors not calibrated"); return; }
    for (int i = 0; i < kMotors; ++i) {
        int m[4];
        for (int j = 0; j < 4; ++j) m[j] = MotorEncoders[i]->getCalibration(j);
        Serial.print(F("cal ")); Serial.print(kNames[i]);
        for (int j = 0; j < 4; ++j) { Serial.print(' '); Serial.print(m[j]); }
        Serial.print(F(" gaps"));
        // Four gaps including the wrap: a sweep that lost steps puts the
        // short gap anywhere, and the wrap gap a quarter of the time.
        for (int j = 0; j < 4; ++j) { Serial.print(' '); Serial.print((m[(j + 1) % 4] - m[j]) & 0x0FFF); }
        Serial.println();
    }
    ok();
}

// The magnet as the AS5600 sees it, for all seven. `health` tells a moving
// magnet from a moving rotor: ANGLE cannot, MAGNITUDE and AGC can.
//   health U=md,ml,mh/agc/mag ...   flags are 1/0; mag is 12-bit field strength
static void cmdHealth() {
    Serial.print(F("health"));
    for (int i = 0; i < 7; ++i) {
        uint8_t st = 0, agc = 0; uint16_t mag = 0;
        int rc = MotorEncoders[i]->readHealth(&st, &agc, &mag);
        Serial.print(' '); Serial.print(kNames[i]); Serial.print('=');
        if (rc != 0) { Serial.print(F("err")); Serial.print(rc); continue; }
        Serial.print((st >> 5) & 1); Serial.print(',');
        Serial.print((st >> 4) & 1); Serial.print(',');
        Serial.print((st >> 3) & 1); Serial.print('/');
        Serial.print(agc);           Serial.print('/');
        Serial.print(mag);
    }
    Serial.println();
    ok();
}

static void cmdNoise(const char* motorArg, const char* countArg) {
    int m = motorArg ? atoi(motorArg) : -1;
    int n = countArg ? atoi(countArg) : 50;
    if (m < 0 || m > 6) { err(5, "noise <motor 0-6> <n>"); return; }
    n = constrain(n, 2, 2000);
    int lo = 4096, hi = -1; long sum = 0; int bad = 0;
    for (int k = 0; k < n; ++k) {
        int v = MotorEncoders[m]->scanChecked();
        if (v < 0) { bad++; continue; }
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        sum += v;
    }
    Serial.print(F("noise ")); Serial.print(kNames[m]);
    Serial.print(F(" n="));    Serial.print(n - bad);
    Serial.print(F(" bad="));  Serial.print(bad);
    Serial.print(F(" min="));  Serial.print(lo);
    Serial.print(F(" max="));  Serial.print(hi);
    Serial.print(F(" mean=")); Serial.print(bad < n ? (float)sum / (n - bad) : 0.0f, 1);
    Serial.println();
    ok();
}

// How far each rotor moves when torque is removed: read energised, drop
// torque, read again. Answers the half-step question directly (a ~9-10 count
// cluster is a rotor relaxing from a half-step to the nearest full-step
// detent) and shows what the de-energised check sees that the energised
// aligner did not.
static void cmdRelax() {
    if (cubeMotors.isHeld()) { err(6, "hold is on; relax needs to drop torque"); return; }
    int a[7], b[7];
    cubeMotors.enableMotors();
    delay(50);
    readAll(a);
    cubeMotors.disableMotors();
    delay(50);
    readAll(b);
    Serial.print(F("relax"));
    for (int i = 0; i < kMotors; ++i) {
        Serial.print(' '); Serial.print(kNames[i]); Serial.print('=');
        Serial.print(a[i]); Serial.print('/'); Serial.print(b[i]); Serial.print('/');
        if (a[i] < 0 || b[i] < 0) Serial.print('?'); else Serial.print(CubeSystem::encError(b[i], a[i]));
    }
    Serial.println();
    ok();
}

static void cmdHome() {
    Cube.clearAbort();
    unsigned long t0 = millis();
    int code = Cube.homeMotors();
    unsigned long dt = millis() - t0;
    Serial.print(F("home code=")); Serial.print(code);
    Serial.print(F(" ms="));       Serial.print(dt);
    Serial.print(F(" en="));       Serial.println(cubeMotors.isHeld() ? 1 : 0);
    int v[7]; readAll(v); printEnc(v);
    if (code) err(20 + code, "homing failed"); else ok();
}

// ----------------------------------------------------------------- moves --

struct RepStats {
    long  n[kMotors];
    long  sumRes[kMotors];
    long  sumSq[kMotors];
    int   maxAbs[kMotors];
    long  moves, notAligned, homings, failures;
    unsigned long moveMs;
};
static RepStats stats;

static void statsReset() { memset(&stats, 0, sizeof(stats)); }

// One physical move with the full record. Returns the move's result code:
// 0 ok, 2x homing failed (the firmware's own numbering), 3 bad token.
static int doMove(const char* tok) {
    if (!validToken(tok)) { err(3, "bad move token"); return 3; }
    Cube.clearAbort();

    int before[7], after[7];
    long pos0[kMotors], pos1[kMotors];
    readAll(before);
    for (int i = 0; i < kMotors; ++i) pos0[i] = cubeMotors.getPos(i);

    unsigned long t0 = micros();
    cubeMotors.executeMove(String(tok));        // physical move + settle
    unsigned long moveUs = micros() - t0;

    for (int i = 0; i < kMotors; ++i) pos1[i] = cubeMotors.getPos(i);
    readAll(after);

    // The firmware's own post-move contract (CubeSystem::executeMove): check
    // every motor against the nearest mark; if any is out, home once.
    bool aligned = Cube.checkAlignment();
    int  homed   = -1;
    if (alignMode && !aligned) homed = Cube.homeMotors();

    const float countsPerStep = 1024.0f / cubeMotors.getTurnStep();

    Serial.print(F("mv "));        Serial.print(tok);
    Serial.print(F(" code="));     Serial.print(homed > 0 ? 20 + homed : 0);
    Serial.print(F(" aligned="));  Serial.print(aligned ? 1 : 0);
    Serial.print(F(" homed="));    if (homed < 0) Serial.print('-'); else Serial.print(homed);
    Serial.print(F(" ms="));       Serial.print(moveUs / 1000.0f, 1);
    for (int i = 0; i < kMotors; ++i) {
        Serial.print(' '); Serial.print(kNames[i]); Serial.print('=');
        Serial.print(before[i]); Serial.print(','); Serial.print(after[i]); Serial.print(',');
        if (before[i] < 0 || after[i] < 0) {
            Serial.print(F("?,?,?"));
            continue;
        }
        // Where the commanded steps should have left the encoder. A "+" step
        // moves the reading DOWN when kAlignStepSign is +1: the aligner steps
        // + to correct a positive (reading above mark) error.
        long  dSteps   = pos1[i] - pos0[i];
        float expected = before[i] - CubeSystem::kAlignStepSign * dSteps * countsPerStep;
        int   expInt   = ((int)lroundf(expected)) & 0x0FFF;
        int   res      = CubeSystem::encError(after[i], expInt);
        int   e; int k = nearestMark(i, after[i], &e);
        Serial.print(res); Serial.print(','); Serial.print(k); Serial.print(','); Serial.print(e);

        if (dSteps != 0) {
            stats.n[i]++; stats.sumRes[i] += res; stats.sumSq[i] += (long)res * res;
            if (abs(res) > stats.maxAbs[i]) stats.maxAbs[i] = abs(res);
        }
    }
    Serial.println();

    stats.moves++;
    stats.moveMs += moveUs / 1000;
    if (!aligned) stats.notAligned++;
    if (homed == 0) stats.homings++;
    if (homed > 0)  stats.failures++;
    return homed > 0 ? 20 + homed : 0;
}

static void printRepSummary() {
    Serial.print(F("rep moves="));      Serial.print(stats.moves);
    Serial.print(F(" notaligned="));    Serial.print(stats.notAligned);
    Serial.print(F(" homings="));       Serial.print(stats.homings);
    Serial.print(F(" failures="));      Serial.print(stats.failures);
    Serial.print(F(" avgms="));         Serial.print(stats.moves ? (float)stats.moveMs / stats.moves : 0.0f, 1);
    Serial.println();
    // Per motor, over the moves in which that motor was commanded: mean and
    // RMS residual in counts, and the worst single move.
    Serial.print(F("res"));
    for (int i = 0; i < kMotors; ++i) {
        Serial.print(' '); Serial.print(kNames[i]); Serial.print('=');
        if (stats.n[i] == 0) { Serial.print(F("-")); continue; }
        float mean = (float)stats.sumRes[i] / stats.n[i];
        float rms  = sqrtf((float)stats.sumSq[i] / stats.n[i]);
        Serial.print(stats.n[i]); Serial.print(','); Serial.print(mean, 1); Serial.print(',');
        Serial.print(rms, 1);     Serial.print(','); Serial.print(stats.maxAbs[i]);
    }
    Serial.println();
}

// argv[0] is the count for rep; for seq it is the first token.
static void runSequence(char** argv, int argc, long reps) {
    if (argc < 1) { err(2, "no moves given"); return; }
    for (int k = 0; k < argc; ++k) if (!validToken(argv[k])) { err(3, "bad move token"); return; }
    while (Serial.available()) Serial.read();      // a stale byte must not stop run 1

    statsReset();
    for (long r = 0; r < reps; ++r) {
        for (int k = 0; k < argc; ++k) {
            if (Serial.available()) {
                Serial.println(F("# stopped by serial input"));
                printRepSummary();
                err(7, "stopped");
                return;
            }
            int code = doMove(argv[k]);
            if (code) {
                printRepSummary();
                err(code, "move failed");
                return;
            }
        }
    }
    printRepSummary();
    ok();
}

// ------------------------------------------------------------ mechanism --

static void cmdServo(const char* which, const char* what) {
    if (!which || !what) { err(2, "servo top|bot ext|ret|partial|eject"); return; }
    bool top = !strcmp(which, "top");
    if (!top && strcmp(which, "bot")) { err(2, "servo top|bot ..."); return; }
    if      (!strcmp(what, "ext"))     { if (top) Cube.topServoExtend();  else Cube.botServoExtend();  }
    else if (!strcmp(what, "ret"))     { if (top) Cube.topServoRetract(); else Cube.botServoRetract(); }
    else if (!strcmp(what, "partial")) { if (top) Cube.topServoPartial(); else Cube.botServoPartial(); }
    else if (!strcmp(what, "eject"))   { if (top) Cube.topServoEject();   else Cube.botServoEject();   }
    else { err(2, "servo ... ext|ret|partial|eject"); return; }
    cmdStatus();
}

static void cmdRing(const char* what) {
    if (!what) { err(2, "ring ext|ret|mid|partial"); return; }
    if      (!strcmp(what, "ext"))     Cube.ringExtend();
    else if (!strcmp(what, "ret"))     Cube.ringRetract();
    else if (!strcmp(what, "mid"))     Cube.ringMiddle();
    else if (!strcmp(what, "partial")) Cube.ringPartial();
    else { err(2, "ring ext|ret|mid|partial"); return; }
    cmdStatus();
}

static void cmdHelp() {
    Serial.println(F("# help status get set enc cal noise relax hold align load unload servo ring home move seq rep stop"));
    Serial.println(F("# see the header of Bench_Tune.ino for the record formats"));
    ok();
}

// ----------------------------------------------------------------- main --

static void dispatch(char* line) {
    char* argv[24];
    int argc = 0;
    for (char* t = strtok(line, " \t\r\n"); t && argc < 24; t = strtok(nullptr, " \t\r\n")) argv[argc++] = t;
    if (argc == 0) { ok(); return; }
    const char* c = argv[0];

    if      (!strcmp(c, "help"))   cmdHelp();
    else if (!strcmp(c, "status")) cmdStatus();
    else if (!strcmp(c, "get"))    cmdGet();
    else if (!strcmp(c, "set"))    cmdSet(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
    else if (!strcmp(c, "enc"))    cmdEnc();
    else if (!strcmp(c, "cal"))    cmdCal();
    else if (!strcmp(c, "noise"))  cmdNoise(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
    else if (!strcmp(c, "health")) cmdHealth();
    else if (!strcmp(c, "relax"))  cmdRelax();
    else if (!strcmp(c, "hold"))   { if (argc > 1 && !strcmp(argv[1], "on")) cubeMotors.holdBegin(); else cubeMotors.holdEnd(); cmdGet(); }
    else if (!strcmp(c, "align"))  { alignMode = (argc > 1 && !strcmp(argv[1], "on")); cmdGet(); }
    else if (!strcmp(c, "load"))   { Cube.clearAbort(); Cube.botServoExtend(); Cube.ringExtend(); Cube.topServoExtend(); cmdStatus(); }
    else if (!strcmp(c, "unload")) { Cube.clearAbort(); Cube.unloadCube(); cmdStatus(); }
    else if (!strcmp(c, "servo"))  cmdServo(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
    else if (!strcmp(c, "ring"))   cmdRing(argc > 1 ? argv[1] : nullptr);
    else if (!strcmp(c, "home"))   cmdHome();
    else if (!strcmp(c, "move"))   { if (argc < 2) err(2, "move <tok>"); else { statsReset(); int e = doMove(argv[1]); if (e) err(e, "move failed"); else ok(); } }
    else if (!strcmp(c, "seq"))    runSequence(argv + 1, argc - 1, 1);
    else if (!strcmp(c, "rep"))    {
        long n = argc > 1 ? atol(argv[1]) : 0;
        if (n < 1 || n > 1000) { err(2, "rep <1-1000> <tok> ..."); return; }
        runSequence(argv + 2, argc - 2, n);
    }
    else if (!strcmp(c, "stop"))   { cubeMotors.holdEnd(); cubeMotors.disableMotors(); cmdStatus(); }
    else err(1, "unknown command");
}

void setup() {
    Cube.begin(CubeSystem::kNoDisplay);
    Cube.debugAlignLog = true;      // every homing prints its pre-correction errors

    Serial.println();
    Serial.println(F("# Bench_Tune ready"));
    cmdStatus();
    cmdGet();
    Serial.println(F("ready"));
}

void loop() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (lineLen > 0) {
                lineBuf[lineLen] = 0;
                lineLen = 0;
                dispatch(lineBuf);
            }
        } else if (lineLen < (int)sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ch;
        } else {
            lineLen = 0;                // overlong line: drop it whole
            err(8, "line too long");
        }
    }
    pumpOnce();                         // keeps the abort chord alive between commands
}
