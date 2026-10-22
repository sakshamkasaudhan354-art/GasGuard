// Gas Leak Zone Alert - public map (OpenStreetMap) version
// Sirf C++. Internet aur 'curl' chahiye (Termux: pkg install curl)
// Compile: g++ gas_alert.cpp -o gas_alert -std=c++11

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const double PI = 3.14159265358979323846;
const double EARTH_R = 6371000.0;
const double PLUME_LENGTH_M = 80000.0;
const double CORE_M = 40000.0;
const double PLUME_HALF_ANGLE = 35.0;
const double SAFE_CIRCLE_M = 1500.0;//red zone sabse phela wala area 
const double CALM_WIND_MS = 0.5;

struct Place {
    string name;
    double lat, lon;
    int zone;
};

double rad(double d) { return d * PI / 180.0; }//radiann mein change karna degree ko
double deg(double r) { return r * 180.0 / PI; }//radian se degree mein change karna 
// harversine formula
double distanceM(double la1, double lo1, double la2, double lo2) {
    double dLat = rad(la2 - la1), dLon = rad(lo2 - lo1);
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(rad(la1)) * cos(rad(la2)) * sin(dLon / 2) * sin(dLon / 2);
    return 2 * EARTH_R * asin(sqrt(a));
}// direction formula

double bearingDeg(double la1, double lo1, double la2, double lo2) {
    double dLon = rad(lo2 - lo1);// in rdian
    double y = sin(dLon) * cos(rad(la2));
    double x = cos(rad(la1)) * sin(rad(la2)) -
               sin(rad(la1)) * cos(rad(la2)) * cos(dLon);
    return fmod(deg(atan2(y, x)) + 360.0, 360.0);// correct quantrant and 0 se 360 mein normalised
}//direction ka meaning 0-north 90-east 180-south 270-west

double angleDiff(double a, double b) {
    double d = fabs(fmod(a - b, 360.0));// diference of two angles
    return d > 180.0 ? 360.0 - d : d;// fabs negative se postive
}//tenery oprator

void destPoint(double lat, double lon, double brg, double dist,
               double& oLat, double& oLon) {
    double d = dist / EARTH_R, b = rad(brg), la = rad(lat), lo = rad(lon);
    double la2 = asin(sin(la) * cos(d) + cos(la) * sin(d) * cos(b));
    double lo2 = lo + atan2(sin(b) * sin(d) * cos(la),
                            cos(d) - sin(la) * sin(la2));
    oLat = deg(la2);
    oLon = deg(lo2);
}

// 0 = safe, 1 = khatra zone, 2 = plume core, 3 = plume door tak
int zoneOf(double fLat, double fLon, double lat, double lon,
           double windFrom, double windSpeed) {
    double d = distanceM(fLat, fLon, lat, lon);
    if (d <= SAFE_CIRCLE_M) return 1;
    if (d > PLUME_LENGTH_M) return 0;
    int z = (d <= CORE_M) ? 2 : 3;
    if (windSpeed < CALM_WIND_MS) return z;
    double downwind = fmod(windFrom + 180.0, 360.0);
    double b = bearingDeg(fLat, fLon, lat, lon);
    return angleDiff(b, downwind) <= PLUME_HALF_ANGLE ? z : 0;
}

// ---------- internet helpers (curl) ----------
string run(const string& cmd) {
    string out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    pclose(f);
    return out;
}

string urlEncode(const string& s) {
    ostringstream o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o << (char)c;
        else o << '%' << uppercase << hex << setw(2) << setfill('0') << (int)c << dec;
    }
    return o.str();
}

// JSON se value nikalne ke chhote helpers
size_t keyPos(const string& s, const string& key) {
    string k = "\"" + key + "\"";
    size_t p = s.find(k);
    if (p == string::npos) return p;
    p += k.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == ':')) p++;
    return p;
}
bool getNum(const string& s, const string& key, double& v) {
    size_t p = keyPos(s, key);
    if (p == string::npos) return false;
    if (s[p] == '"') p++;
    v = atof(s.c_str() + p);
    return true;
}
bool getStr(const string& s, const string& key, string& v) {
    size_t p = keyPos(s, key);
    if (p == string::npos || s[p] != '"') return false;
    size_t e = s.find('"', p + 1);
    if (e == string::npos) return false;
    v = s.substr(p + 1, e - p - 1);
    return true;
}

bool geocode(const string& q, double& lat, double& lon, string& full) {
    string cmd = "curl -s -m 20 -A GasLeakAlert \"https://nominatim.openstreetmap.org/"
                 "search?format=json&limit=1&q=" + urlEncode(q) + "\"";
    string r = run(cmd);
    if (!getNum(r, "lat", lat) || !getNum(r, "lon", lon)) return false;
    if (!getStr(r, "display_name", full)) full = q;
    return true;
}

bool liveWind(double lat, double lon, double& from, double& spd) {
    ostringstream u;
    u << fixed << setprecision(4)
      << "curl -s -m 20 \"https://api.open-meteo.com/v1/forecast?latitude=" << lat
      << "&longitude=" << lon
      << "&current=wind_speed_10m,wind_direction_10m&wind_speed_unit=ms\"";
    string r = run(u.str());
    size_t p = r.find("\"current\"");
    if (p == string::npos) return false;
    string cur = r.substr(p);
    return getNum(cur, "wind_speed_10m", spd) && getNum(cur, "wind_direction_10m", from);
}

vector<Place> fetchPlaces(double lat, double lon) {
    vector<Place> v;
    ostringstream q;
    q << fixed << setprecision(6)
      << "curl -s -m 30 -A GasLeakAlert --data-urlencode "
      << "'data=[out:json][timeout:25];node(around:900," << lat << "," << lon
      << ")[amenity~\"school|college|hospital|clinic|place_of_worship|marketplace\"];out 40;' "
      << "https://overpass-api.de/api/interpreter";
    string r = run(q.str());
    size_t pos = r.find("\"type\"");
    while (pos != string::npos) {
        size_t nxt = r.find("\"type\"", pos + 6);
        string seg = r.substr(pos, nxt == string::npos ? string::npos : nxt - pos);
        Place p;
        string nm, am;
        if (getNum(seg, "lat", p.lat) && getNum(seg, "lon", p.lon)) {
            getStr(seg, "amenity", am);
            if (!getStr(seg, "name", nm)) nm = am.empty() ? "Jagah" : am;
            p.name = nm + (am.empty() ? "" : " (" + am + ")");
            p.zone = 0;
            v.push_back(p);
        }
        pos = nxt;
    }
    return v;
}

// ---------- GeoJSON (public map par dikhane ke liye) ----------
string jsonEsc(const string& s) {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"' || s[i] == '\\') o += '\\';
        o += s[i];
    }
    return o;
}

string circleRing(double lat, double lon, double r) {
    ostringstream o;
    o << fixed << setprecision(6) << "[";
    for (int i = 0; i <= 24; i++) {
        double a, b;
        destPoint(lat, lon, (i % 24) * 15.0, r, a, b);
        if (i) o << ",";
        o << "[" << b << "," << a << "]";
    }
    o << "]";
    return o.str();
}

string sectorRing(double lat, double lon, double r, double center, double half) {
    ostringstream o;
    o << fixed << setprecision(6) << "[[" << lon << "," << lat << "]";
    for (int k = 0; k <= 10; k++) {
        double a, b;
        destPoint(lat, lon, center - half + 2 * half * k / 10.0, r, a, b);
        o << ",[" << b << "," << a << "]";
    }
    o << ",[" << lon << "," << lat << "]]";
    return o.str();
}

string polyFeature(const string& ring, const string& name, const string& col) {
    return "{\"type\":\"Feature\",\"properties\":{\"name\":\"" + name +
           "\",\"fill\":\"" + col + "\",\"fill-opacity\":0.45,\"stroke\":\"" + col +
           "\"},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[" + ring + "]}}";
}

string pointFeature(double lat, double lon, const string& name, const string& col) {
    ostringstream o;
    o << fixed << setprecision(6)
      << "{\"type\":\"Feature\",\"properties\":{\"name\":\"" << jsonEsc(name)
      << "\",\"marker-color\":\"" << col << "\"},\"geometry\":{\"type\":\"Point\","
      << "\"coordinates\":[" << lon << "," << lat << "]}}";
    return o.str();
}

const char* ZCOL[4] = {"#2f8f5b", "#d8362b", "#ee8a1c", "#e8c81f"};

int main() {
    cout << "Factory ka naam ya address (jaise: Tata Steel Jamshedpur): ";
    string query;
    getline(cin, query);

    double fLat, fLon;
    string full;
    cout << "Public map (OpenStreetMap) par dhoondh rahe hain...\n";
    if (!geocode(query, fLat, fLon, full)) {
        cout << "Jagah nahi mili. Internet/curl check karein ya naam aur clear likhein.\n";
        return 1;
    }
    cout << "Factory mili: " << full << "\n";
    cout << fixed << setprecision(5) << "Location: " << fLat << ", " << fLon << "\n\n";

    double windFrom = 0, windSpeed = 0;
    cout << "Hawa: 1 = live (internet se), 2 = manual: ";
    int mode;
    cin >> mode;
    if (mode == 1 && liveWind(fLat, fLon, windFrom, windSpeed)) {
        cout << "Live hawa: " << windFrom << " deg se, " << windSpeed << " m/s\n";
    } else {
        if (mode == 1) cout << "Live hawa nahi mili, manual daalein.\n";
        cout << "Hawa kis direction SE aa rahi hai (0-360, 0=North, 90=East): ";
        cin >> windFrom;
        cout << "Hawa ki speed (m/s): ";
        cin >> windSpeed;
    }

    cout << "\nAas-paas ke school/hospital/mandir/market dhoondh rahe hain...\n";
    vector<Place> places = fetchPlaces(fLat, fLon);
    if (places.empty()) {
        cout << "Real jagah nahi mili, sample logon se test kar rahe hain.\n";
        double mLon = 111320.0 * cos(rad(fLat));
        const char* nm[6] = {"Ramesh", "Sita", "Aman", "Priya", "Karan", "Meera"};
        double dn[6] = {334, -334, 0, 0, 55, -480};
        double de[6] = {0, 0, 391, -391, 55, -250};
        for (int i = 0; i < 6; i++) {
            Place p;
            p.name = nm[i];
            p.lat = fLat + dn[i] / 111320.0;
            p.lon = fLon + de[i] / mLon;
            p.zone = 0;
            places.push_back(p);
        }
    }

    for (size_t i = 0; i < places.size(); i++)
        places[i].zone = zoneOf(fLat, fLon, places[i].lat, places[i].lon,
                                windFrom, windSpeed);

    const char* title[4] = {
        "SAFE ZONE",
        "ZONE 1 - KHATRA (factory se 150 m)",
        "ZONE 2 - PLUME RAASTA (hawa ki taraf, 150-400 m)",
        "ZONE 3 - DOOR TAK ASAR (hawa ki taraf, 400-800 m)"};
    const char* guide[4] = {
        "  - Abhi khatra nahi, alert par nazar rakhein.\n"
        "  - Hawa badle to zone badal sakta hai, dobara check karein.\n",
        "  - Turant jagah chhodein, factory ki taraf nahi jaana.\n"
        "  - Hawa ke side (crosswind) direction mein door jayein.\n"
        "  - Chingari, switch, gaadi start na karein. Nak-munh geele kapde se dhakein.\n",
        "  - Plume ke raaste se side mein hatein (hawa ke perpendicular).\n"
        "  - Side mein na ja sakein to andar jayein, khidki-darwaze band karein.\n"
        "  - AC/pankhe/vent band. Bachon aur buzurgon ko pehle andar.\n",
        "  - Andar rahein, khidki-darwaze aur vent band karein.\n"
        "  - AC band, gila kapda darwaze ke neeche rakhein.\n"
        "  - Sarkari alert ka intezaar karein. Jalan/saans ki dikkat ho to doctor.\n"};

    int order[4] = {1, 2, 3, 0};
    cout << "\n==================== ZONE REPORT ====================\n";
    for (int k = 0; k < 4; k++) {
        int z = order[k];
        cout << "\n" << title[z] << "\n";
        bool any = false;
        for (size_t i = 0; i < places.size(); i++) {
            if (places[i].zone == z) {
                cout << "  * " << places[i].name << " ("
                     << (int)distanceM(fLat, fLon, places[i].lat, places[i].lon) << " m)\n";
                any = true;
            }
        }
        if (!any) cout << "  (koi nahi)\n";
        cout << guide[z];
    }

    // GeoJSON file banao
    string down;
    double dn = fmod(windFrom + 180.0, 360.0);
    string gj = "{\"type\":\"FeatureCollection\",\"features\":[";
    if (windSpeed < CALM_WIND_MS) {
        gj += polyFeature(circleRing(fLat, fLon, PLUME_LENGTH_M), "Zone 3", ZCOL[3]) + ",";
        gj += polyFeature(circleRing(fLat, fLon, CORE_M), "Zone 2", ZCOL[2]) + ",";
    } else {
        gj += polyFeature(sectorRing(fLat, fLon, PLUME_LENGTH_M, dn, PLUME_HALF_ANGLE), "Zone 3", ZCOL[3]) + ",";
        gj += polyFeature(sectorRing(fLat, fLon, CORE_M, dn, PLUME_HALF_ANGLE), "Zone 2", ZCOL[2]) + ",";
    }
    gj += polyFeature(circleRing(fLat, fLon, SAFE_CIRCLE_M), "Zone 1", ZCOL[1]) + ",";
    gj += pointFeature(fLat, fLon, "FACTORY", "#000000");
    for (size_t i = 0; i < places.size(); i++)
        gj += "," + pointFeature(places[i].lat, places[i].lon, places[i].name, ZCOL[places[i].zone]);
    gj += "]}";

    ofstream("zones.geojson") << gj;
    ofstream("map_link.txt") << "https://geojson.io/#data=data:application/json," << urlEncode(gj) << "\n";

    cout << "\n==================== PUBLIC MAP ====================\n";
    cout << "Factory real map par:\n"
         << "https://www.openstreetmap.org/?mlat=" << fLat << "&mlon=" << fLon
         << "#map=16/" << fLat << "/" << fLon << "\n\n";
    cout << "Zones ke saath map: 'map_link.txt' ka link browser mein kholein\n"
         << "(ya zones.geojson ko geojson.io mein Open karein).\n"
         << "Termux mein: termux-open-url \"$(cat map_link.txt)\"\n";
    return 0;
}