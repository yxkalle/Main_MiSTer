#include "n64.h"
#include "../../menu.h"
#include "../../user_io.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "lib/md5/md5.h"

// Simple hash function, see: https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function

static constexpr uint64_t FNV_PRIME = 0x100000001b3;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;

static constexpr uint64_t fnv_hash(const char* s, uint64_t h = FNV_OFFSET_BASIS)
{
	if (s) while (uint8_t a = *(s++)) h = (h ^ a) * FNV_PRIME;
	return h;
}

enum class MemoryType
{
	NONE = 0,
	EEPROM_512,
	EEPROM_2k,
	SRAM_32k,
	SRAM_96k,
	FLASH_128k
};

enum class CIC
{
	CIC_NUS_6101 = 0,
	CIC_NUS_6102,
	CIC_NUS_7101,
	CIC_NUS_7102,
	CIC_NUS_6103,
	CIC_NUS_7103,
	CIC_NUS_6105,
	CIC_NUS_7105,
	CIC_NUS_6106,
	CIC_NUS_7106,
	CIC_NUS_8303,
	CIC_NUS_8401,
	CIC_NUS_5167,
	CIC_NUS_DDUS
};

enum class SystemType
{
	NTSC = 0,
	PAL
};

enum class RomFormat
{
	UNKNOWN = 0,
	BIG_ENDIAN,
	BYTE_SWAPPED,
	LITTLE_ENDIAN,
};

enum class AutoDetect
{
	ON = 0,
	OFF = 1,
};

static RomFormat detectRomFormat(const uint8_t* data)
{
	// data should be aligned
	const uint32_t val = *(uint32_t*)data;

	// the following checks assume we're on a little-endian platform
	// for each check, the first value is for regular roms, the 2nd is for 64DD images
	if (val == 0x40123780 || val == 0x40072780) return RomFormat::BIG_ENDIAN;
	if (val == 0x12408037 || val == 0x07408027) return RomFormat::BYTE_SWAPPED;
	if (val == 0x80371240 || val == 0x80270740) return RomFormat::LITTLE_ENDIAN;

	return RomFormat::UNKNOWN;
}

static void normalizeData(uint8_t* data, size_t size, RomFormat format)
{
	switch (format)
	{
	case RomFormat::BYTE_SWAPPED:
		for (size_t i = 0; i < size; i += 2)
		{
			auto c0 = data[0];
			auto c1 = data[1];
			data[0] = c1;
			data[1] = c0;
			data += 2;
		}
		break;
	case RomFormat::LITTLE_ENDIAN:
		for (size_t i = 0; i < size; i += 4)
		{
			auto c0 = data[0];
			auto c1 = data[1];
			auto c2 = data[2];
			auto c3 = data[3];
			data[0] = c3;
			data[1] = c2;
			data[2] = c1;
			data[3] = c0;
			data += 4;
		}
		break;
	default:
	{
		// nothing to do
	}
	}
}

static void normalizeString(char* s)
{
	// change the string to lower-case
	while (*s) { *s = tolower(*s); ++s; }
}

static bool detect_rom_settings_in_db(const char* lookup_hash, const char* db_file_name)
{
	fileTextReader reader = {};

	char file_path[1024];
	sprintf(file_path, "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, file_path))
	{
		printf("Failed to open N64 data file %s\n", file_path);
		return false;
	}

	char tags[128];

	const char* line;
	while ((line = FileReadLine(&reader)))
	{
		// skip the line if it doesn't start with our hash
		if (strncmp(lookup_hash, line, 32) != 0)
			continue;

		if (sscanf(line, "%*s %s", tags) != 1)
		{
			printf("No tags found.\n");
			continue;
		}

		printf("Found ROM entry: %s\n", line);

		MemoryType save_type = MemoryType::NONE;
		SystemType system_type = SystemType::NTSC;
		CIC cic = CIC::CIC_NUS_6102;
		bool cpak = false;
		bool rpak = false;
		bool tpak = false;
		bool rtc = false;

		const char separator[] = "|";

		for (char* tag = strtok(tags, separator); tag; tag = strtok(nullptr, separator))
		{
			printf("Tag: %s\n", tag);

			normalizeString(tag);
			switch (fnv_hash(tag))
			{
				case fnv_hash("eeprom512"): save_type = MemoryType::EEPROM_512; break;
				case fnv_hash("eeprom2k"): save_type = MemoryType::EEPROM_2k; break;
				case fnv_hash("sram32k"): save_type = MemoryType::SRAM_32k; break;
				case fnv_hash("sram96k"): save_type = MemoryType::SRAM_96k; break;
				case fnv_hash("flash128k"): save_type = MemoryType::FLASH_128k; break;
				case fnv_hash("ntsc"): system_type = SystemType::NTSC; break;
				case fnv_hash("pal"): system_type = SystemType::PAL; break;
				case fnv_hash("cpak"): cpak = true; break;
				case fnv_hash("rpak"): rpak = true; break;
				case fnv_hash("tpak"): tpak = true; break;
				case fnv_hash("rtc"): rtc = true; break;
				case fnv_hash("cic6101"): cic = CIC::CIC_NUS_6101; break;
				case fnv_hash("cic6102"): cic = CIC::CIC_NUS_6102; break;
				case fnv_hash("cic6103"): cic = CIC::CIC_NUS_6103; break;
				case fnv_hash("cic6105"): cic = CIC::CIC_NUS_6105; break;
				case fnv_hash("cic6106"): cic = CIC::CIC_NUS_6106; break;
				case fnv_hash("cic7101"): cic = CIC::CIC_NUS_7101; break;
				case fnv_hash("cic7102"): cic = CIC::CIC_NUS_7102; break;
				case fnv_hash("cic7103"): cic = CIC::CIC_NUS_7103; break;
				case fnv_hash("cic7105"): cic = CIC::CIC_NUS_7105; break;
				case fnv_hash("cic7106"): cic = CIC::CIC_NUS_7106; break;
				case fnv_hash("cic8303"): cic = CIC::CIC_NUS_8303; break;
				case fnv_hash("cic8401"): cic = CIC::CIC_NUS_8401; break;
				case fnv_hash("cic5167"): cic = CIC::CIC_NUS_5167; break;
				case fnv_hash("cicDDUS"): cic = CIC::CIC_NUS_DDUS; break;
				default: printf("Unknown tag: %s\n", tag);
			}
		}

		printf("System: %d, Save Type: %d, CIC: %d, CPak: %d, RPak: %d, TPak %d, RTC: %d\n", (int)system_type, (int)save_type, (int)cic, cpak, rpak, tpak, rtc);

		const auto auto_detect = (AutoDetect)user_io_status_get("[64]");

		if (auto_detect == AutoDetect::ON)
		{
			printf("Auto-detect is ON, updating OSD settings\n");

			user_io_status_set("[80:79]", (uint32_t)system_type);
			user_io_status_set("[68:65]", (uint32_t)cic);
			user_io_status_set("[71]", (uint32_t)cpak);
			user_io_status_set("[72]", (uint32_t)rpak);
			user_io_status_set("[73]", (uint32_t)tpak);
			user_io_status_set("[74]", (uint32_t)rtc);
			user_io_status_set("[77:75]", (uint32_t)save_type);
		}
		else
		{
			printf("Auto-detect is OFF, not updating OSD settings\n");
		}

		return true;
	}

	return false;
}

static const char* DB_FILE_NAMES[] =
{
	"N64-database.txt",
	"N64-database_user.txt",
};

static bool detect_rom_settings_in_dbs(const char* lookup_hash)
{
	for (const char* db_file_name : DB_FILE_NAMES)
	{
		if (detect_rom_settings_in_db(lookup_hash, db_file_name))
			return true;
	}
	return false;
}

static uint8_t detect_rom_settings_from_first_chunk(char* id, char region_code, uint8_t revision, uint64_t crc)
{
	MemoryType save_type = MemoryType::NONE;
	SystemType system_type = SystemType::NTSC;
	CIC cic;
	bool cpak = false;
	bool rpak = false;
	bool tpak = false;
	bool rtc = false;

	const auto auto_detect = (AutoDetect)user_io_status_get("[64]");

	if (auto_detect != AutoDetect::ON)
	{
		printf("Auto-detect is OFF, not updating OSD settings\n");
		return 0;
	}

	switch (region_code)
	{
		case 'D': //Germany
		case 'F': //France
		case 'H': //Netherlands (Dutch)
		case 'I': //Italy
		case 'L': //Gateway 64
		case 'P': //Europe
		case 'S': //Spain
		case 'U': //Australia
		case 'W': //Scandinavia
		case 'X': //Europe
		case 'Y': //Europe
			system_type = SystemType::PAL; break;
	}

	// the following checks assume we're on a little-endian platform
	switch (crc)
	{
		case UINT64_C(0x000000a316adc55a):
		case UINT64_C(0x000000039c981107): // hcs64's CIC-6102 IPL3 replacement
		case UINT64_C(0x000000a30dacd530): // Unknown. Used in SM64 hacks
		case UINT64_C(0x000000d2828281b0): // Unknown. Used in some homebrew
		case UINT64_C(0x0000009acc31e644): // Unknown. Used in some betas and homebrew. Dev boot code?
			cic = system_type == SystemType::NTSC
				? CIC::CIC_NUS_6102
				: CIC::CIC_NUS_7101; break;
		case UINT64_C(0x000000a405397b05): cic = CIC::CIC_NUS_7102; system_type = SystemType::PAL; break;
		case UINT64_C(0x000000a0f26f62fe): cic = CIC::CIC_NUS_6101; system_type = SystemType::NTSC; break;
		case UINT64_C(0x000000a9229d7c45):
			cic = system_type == SystemType::NTSC
				? CIC::CIC_NUS_6103
				: CIC::CIC_NUS_7103; break;
		case UINT64_C(0x000000f8b860ed00):
			cic = system_type == SystemType::NTSC
				? CIC::CIC_NUS_6105
				: CIC::CIC_NUS_7105; break;
		case UINT64_C(0x000000ba5ba4b8cd):
			cic = system_type == SystemType::NTSC
				? CIC::CIC_NUS_6106
				: CIC::CIC_NUS_7106; break;
		case UINT64_C(0x0000012daafc8aab): cic = CIC::CIC_NUS_5167; break;
		case UINT64_C(0x000000a9df4b39e1): cic = CIC::CIC_NUS_8303; break;
		case UINT64_C(0x000000aa764e39e1): cic = CIC::CIC_NUS_8401; break;
		case UINT64_C(0x000000abb0b739e1): cic = CIC::CIC_NUS_DDUS; break;
		default: return 1;
	}

	printf("System: %d, Save Type: %d, CIC: %d, CPak: %d, RPak: %d, TPak %d, RTC: %d\n", (int)system_type, (int)save_type, (int)cic, cpak, rpak, tpak, rtc);
	printf("Auto-detect is ON, updating OSD settings\n");

	user_io_status_set("[80:79]", (uint32_t)system_type);
	user_io_status_set("[68:65]", (uint32_t)cic);

	switch (fnv_hash(id))
	{
		//512B EEPROM
		case fnv_hash("NTW"): save_type = MemoryType::EEPROM_512; cpak = true; break; //64 de Hakken!! Tamagotchi
		case fnv_hash("NHF"): save_type = MemoryType::EEPROM_512; break; //64 Hanafuda: Tenshi no Yakusoku
		case fnv_hash("NOS"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //64 Oozumou
		case fnv_hash("NTC"): save_type = MemoryType::EEPROM_512; rpak = true; break; //64 Trump Collection
		case fnv_hash("NER"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Aero Fighters Assault [Sonic Wings Assault (J)]
		case fnv_hash("NAG"): save_type = MemoryType::EEPROM_512; cpak = true; break; //AeroGauge
		case fnv_hash("NAB"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Air Boarder 64
		case fnv_hash("NS3"): save_type = MemoryType::EEPROM_512; cpak = true; break; //AI Shougi 3
		case fnv_hash("NTN"): save_type = MemoryType::EEPROM_512; break; //All Star Tennis '99
		case fnv_hash("NBN"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Bakuretsu Muteki Bangaioh
		case fnv_hash("NBK"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Banjo-Kazooie [Banjo to Kazooie no Daiboken (J)]
		case fnv_hash("NFH"): save_type = MemoryType::EEPROM_512; rpak = true; break; //In-Fisherman Bass Hunter 64 
		case fnv_hash("NMU"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Big Mountain 2000
		case fnv_hash("NBC"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Blast Corps
		case fnv_hash("NBH"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Body Harvest
		case fnv_hash("NHA"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Bomberman 64: Arcade Edition (J)
		case fnv_hash("NBM"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Bomberman 64 [Baku Bomberman (J)]
		case fnv_hash("NBV"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Bomberman 64: The Second Attack! [Baku Bomberman 2 (J)]
		case fnv_hash("NBD"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Bomberman Hero [Mirian Ojo o Sukue! (J)]
		case fnv_hash("NCT"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Chameleon Twist
		case fnv_hash("NCH"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Chopper Attack
		case fnv_hash("NCG"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; tpak = true; break; //Choro Q 64 II - Hacha Mecha Grand Prix Race (J)
		case fnv_hash("NP2"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Chou Kuukan Night Pro Yakyuu King 2 (J)
		case fnv_hash("NXO"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Cruis'n Exotica
		case fnv_hash("NCU"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Cruis'n USA
		case fnv_hash("NCX"): save_type = MemoryType::EEPROM_512; break; //Custom Robo
		case fnv_hash("NDY"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Diddy Kong Racing
		case fnv_hash("NDQ"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Disney's Donald Duck - Goin' Quackers [Quack Attack (E)]
		case fnv_hash("NDR"): save_type = MemoryType::EEPROM_512; break; //Doraemon: Nobita to 3tsu no Seireiseki
		case fnv_hash("NN6"): save_type = MemoryType::EEPROM_512; break; //Dr. Mario 64
		case fnv_hash("NDU"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Duck Dodgers starring Daffy Duck
		case fnv_hash("NJM"): save_type = MemoryType::EEPROM_512; break; //Earthworm Jim 3D
		case fnv_hash("NFW"): save_type = MemoryType::EEPROM_512; rpak = true; break; //F-1 World Grand Prix
		case fnv_hash("NF2"): save_type = MemoryType::EEPROM_512; rpak = true; break; //F-1 World Grand Prix II
		case fnv_hash("NKA"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Fighters Destiny [Fighting Cup (J)]
		case fnv_hash("NFG"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Fighter Destiny 2
		case fnv_hash("NGL"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Getter Love!!
		case fnv_hash("NGV"): save_type = MemoryType::EEPROM_512; break; //Glover
		case fnv_hash("NGE"): save_type = MemoryType::EEPROM_512; rpak = true; break; //GoldenEye 007
		case fnv_hash("NHP"): save_type = MemoryType::EEPROM_512; break; //Heiwa Pachinko World 64
		case fnv_hash("NPG"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Hey You, Pikachu! [Pikachu Genki Dechu (J)]
		case fnv_hash("NIJ"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Indiana Jones and the Infernal Machine
		case fnv_hash("NIC"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Indy Racing 2000
		case fnv_hash("NFY"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Kakutou Denshou: F-Cup Maniax
		case fnv_hash("NKI"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Killer Instinct Gold
		case fnv_hash("NLL"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Last Legion UX
		case fnv_hash("NLR"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Lode Runner 3-D
		case fnv_hash("NKT"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Mario Kart 64
		case fnv_hash("CLB"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Mario Party (NTSC)
		case fnv_hash("NLB"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Mario Party (PAL)
		case fnv_hash("NMW"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Mario Party 2
		case fnv_hash("NML"): save_type = MemoryType::EEPROM_512; rpak = true; tpak = true; break; //Mickey's Speedway USA [Mickey no Racing Challenge USA (J)]
		case fnv_hash("NTM"): save_type = MemoryType::EEPROM_512; break; //Mischief Makers [Yuke Yuke!! Trouble Makers (J)]
		case fnv_hash("NMI"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Mission: Impossible
		case fnv_hash("NMG"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Monaco Grand Prix [Racing Simulation 2 (G)]
		case fnv_hash("NMO"): save_type = MemoryType::EEPROM_512; break; //Monopoly
		case fnv_hash("NMS"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Morita Shougi 64
		case fnv_hash("NMR"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Multi-Racing Championship
		case fnv_hash("NCR"): save_type = MemoryType::EEPROM_512; cpak = true; break; //Penny Racers [Choro Q 64 (J)]
		case fnv_hash("NEA"): save_type = MemoryType::EEPROM_512; break; //PGA European Tour
		case fnv_hash("NPW"): save_type = MemoryType::EEPROM_512; break; //Pilotwings 64
		case fnv_hash("NPY"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Puyo Puyo Sun 64
		case fnv_hash("NPT"): save_type = MemoryType::EEPROM_512; rpak = true; tpak = true; break; //Puyo Puyon Party
		case fnv_hash("NRA"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Rally '99 (J)
		case fnv_hash("NWQ"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Rally Challenge 2000
		case fnv_hash("NSU"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Rocket: Robot on Wheels
		case fnv_hash("NSN"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Snow Speeder (J)
		case fnv_hash("NK2"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Snowboard Kids 2 [Chou Snobow Kids (J)]
		case fnv_hash("NSV"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Space Station Silicon Valley
		case fnv_hash("NFX"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Star Fox 64 [Lylat Wars (E)]
		case fnv_hash("NS6"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Star Soldier: Vanishing Earth
		case fnv_hash("NNA"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Star Wars Episode I: Battle for Naboo
		case fnv_hash("NRS"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Star Wars: Rogue Squadron [Shutsugeki! Rogue Chuutai (J)]
		case fnv_hash("NSW"): save_type = MemoryType::EEPROM_512; break; //Star Wars: Shadows of the Empire [Teikoku no Kage (J)]
		case fnv_hash("NSC"): save_type = MemoryType::EEPROM_512; break; //Starshot: Space Circus Fever
		case fnv_hash("NSA"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Sonic Wings Assault (J)
		case fnv_hash("NB6"): save_type = MemoryType::EEPROM_512; cpak = true; tpak = true; break; //Super B-Daman: Battle Phoenix 64
		case fnv_hash("NSS"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Super Robot Spirits
		case fnv_hash("NTX"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Taz Express
		case fnv_hash("NT6"): save_type = MemoryType::EEPROM_512; break; //Tetris 64
		case fnv_hash("NTP"): save_type = MemoryType::EEPROM_512; break; //Tetrisphere
		case fnv_hash("NTJ"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Tom & Jerry in Fists of Fury
		case fnv_hash("NRC"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Top Gear Overdrive
		case fnv_hash("NTR"): save_type = MemoryType::EEPROM_512; cpak = true; rpak = true; break; //Top Gear Rally (J + E)
		case fnv_hash("NTB"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Transformers: Beast Wars Metals 64 (J)
		case fnv_hash("NGU"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Tsumi to Batsu: Hoshi no Keishousha (Sin and Punishment)
		case fnv_hash("NIR"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Utchan Nanchan no Hono no Challenger: Denryuu Ira Ira Bou
		case fnv_hash("NVL"): save_type = MemoryType::EEPROM_512; rpak = true; break; //V-Rally Edition '99
		case fnv_hash("NVY"): save_type = MemoryType::EEPROM_512; rpak = true; break; //V-Rally Edition '99 (J)
		case fnv_hash("NWC"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Wild Choppers
		case fnv_hash("NAD"): save_type = MemoryType::EEPROM_512; break; //Worms Armageddon (U)
		case fnv_hash("NWU"): save_type = MemoryType::EEPROM_512; break; //Worms Armageddon (E)
		case fnv_hash("NYK"): save_type = MemoryType::EEPROM_512; rpak = true; break; //Yakouchuu II: Satsujin Kouro
		case fnv_hash("NMZ"): save_type = MemoryType::EEPROM_512; break; //Zool - Majou Tsukai Densetsu (J)

		//2KB EEPROM
		case fnv_hash("NB7"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Banjo-Tooie [Banjo to Kazooie no Daiboken 2 (J)]
		case fnv_hash("NGT"): save_type = MemoryType::EEPROM_2k; cpak = true; rpak = true; break; //City Tour GrandPrix - Zen Nihon GT Senshuken
		case fnv_hash("NFU"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Conker's Bad Fur Day
		case fnv_hash("NCW"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Cruis'n World
		case fnv_hash("NCZ"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Custom Robo V2
		case fnv_hash("ND6"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Densha de Go! 64
		case fnv_hash("NDO"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Donkey Kong 64
		case fnv_hash("ND2"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Doraemon 2: Nobita to Hikari no Shinden
		case fnv_hash("N3D"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Doraemon 3: Nobita no Machi SOS!
		case fnv_hash("NMX"): save_type = MemoryType::EEPROM_2k; cpak = true; rpak = true; break; //Excitebike 64
		case fnv_hash("NGC"): save_type = MemoryType::EEPROM_2k; cpak = true; rpak = true; break; //GT 64: Championship Edition
		case fnv_hash("NIM"): save_type = MemoryType::EEPROM_2k; break; //Ide Yosuke no Mahjong Juku
		case fnv_hash("NNB"): save_type = MemoryType::EEPROM_2k; cpak = true; rpak = true; break; //Kobe Bryant in NBA Courtside
		case fnv_hash("NMV"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Mario Party 3
		case fnv_hash("NM8"): save_type = MemoryType::EEPROM_2k; rpak = true; tpak = true; break; //Mario Tennis
		case fnv_hash("NEV"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Neon Genesis Evangelion
		case fnv_hash("NPP"): save_type = MemoryType::EEPROM_2k; cpak = true; break; //Parlor! Pro 64: Pachinko Jikki Simulation Game
		case fnv_hash("NUB"): save_type = MemoryType::EEPROM_2k; cpak = true; tpak = true; break; //PD Ultraman Battle Collection 64
		case fnv_hash("NPD"): save_type = MemoryType::EEPROM_2k; cpak = true; rpak = true; tpak = true; break; //Perfect Dark
		case fnv_hash("NRZ"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Ridge Racer 64
		case fnv_hash("NR7"): save_type = MemoryType::EEPROM_2k; tpak = true; break; //Robot Poncots 64: 7tsu no Umi no Caramel
		case fnv_hash("NEP"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Star Wars Episode I: Racer
		case fnv_hash("NYS"): save_type = MemoryType::EEPROM_2k; rpak = true; break; //Yoshi's Story

		//32KB SRAM
		case fnv_hash("NTE"): save_type = MemoryType::SRAM_32k; rpak = true; break; //1080 Snowboarding
		case fnv_hash("NVB"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Bass Rush - ECOGEAR PowerWorm Championship (J)
		case fnv_hash("NB5"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Biohazard 2 (J)
		case fnv_hash("CFZ"): save_type = MemoryType::SRAM_32k; rpak = true; break; //F-Zero X (J)
		case fnv_hash("NFZ"): save_type = MemoryType::SRAM_32k; rpak = true; break; //F-Zero X (U + E)
		case fnv_hash("NSI"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Fushigi no Dungeon: Fuurai no Shiren 2
		case fnv_hash("NG6"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Ganmare Goemon: Dero Dero Douchuu Obake Tenkomori
		case fnv_hash("NGP"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Goemon: Mononoke Sugoroku
		case fnv_hash("NYW"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Harvest Moon 64
		case fnv_hash("NHY"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //Hybrid Heaven (J)
		case fnv_hash("NIB"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Itoi Shigesato no Bass Tsuri No. 1 Kettei Ban!
		case fnv_hash("NPS"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //Jikkyou J.League 1999: Perfect Striker 2
		case fnv_hash("NPA"): save_type = MemoryType::SRAM_32k; cpak = true; tpak = true; break; //Jikkyou Powerful Pro Yakyuu 2000
		case fnv_hash("NP4"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Jikkyou Powerful Pro Yakyuu 4
		case fnv_hash("NJ5"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Jikkyou Powerful Pro Yakyuu 5
		case fnv_hash("NP6"): save_type = MemoryType::SRAM_32k; cpak = true; tpak = true; break; //Jikkyou Powerful Pro Yakyuu 6
		case fnv_hash("NPE"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Jikkyou Powerful Pro Yakyuu Basic Ban 2001
		case fnv_hash("NJG"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Jinsei Game 64
		case fnv_hash("CZL"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Legend of Zelda: Ocarina of Time [Zelda no Densetsu - Toki no Ocarina (J)]
		case fnv_hash("NZL"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Legend of Zelda: Ocarina of Time (E)
		case fnv_hash("NKG"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //Major League Baseball featuring Ken Griffey Jr.
		case fnv_hash("NMF"): save_type = MemoryType::SRAM_32k; rpak = true; tpak = true; break; //Mario Golf 64
		case fnv_hash("NRI"): save_type = MemoryType::SRAM_32k; cpak = true; break; //New Tetris, The
		case fnv_hash("NUT"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; tpak = true; break; //Nushi Zuri 64
		case fnv_hash("NUM"): save_type = MemoryType::SRAM_32k; rpak = true; tpak = true; break; //Nushi Zuri 64: Shiokaze ni Notte
		case fnv_hash("NOB"): save_type = MemoryType::SRAM_32k; break; //Ogre Battle 64: Person of Lordly Caliber
		case fnv_hash("CPS"): save_type = MemoryType::SRAM_32k; tpak = true; break; //Pocket Monsters Stadium (J)
		case fnv_hash("NPM"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Premier Manager 64
		case fnv_hash("NRE"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Resident Evil 2
		case fnv_hash("NAL"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Super Smash Bros. [Nintendo All-Star! Dairantou Smash Brothers (J)]
		case fnv_hash("NT3"): save_type = MemoryType::SRAM_32k; cpak = true; break; //Shin Nihon Pro Wrestling - Toukon Road 2 - The Next Generation (J)
		case fnv_hash("NS4"): save_type = MemoryType::SRAM_32k; cpak = true; tpak = true; break; //Super Robot Taisen 64
		case fnv_hash("NA2"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //Virtual Pro Wrestling 2
		case fnv_hash("NVP"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //Virtual Pro Wrestling 64
		case fnv_hash("NWL"): save_type = MemoryType::SRAM_32k; rpak = true; break; //Waialae Country Club: True Golf Classics
		case fnv_hash("NW2"): save_type = MemoryType::SRAM_32k; rpak = true; break; //WCW-nWo Revenge
		case fnv_hash("NWX"): save_type = MemoryType::SRAM_32k; cpak = true; rpak = true; break; //WWF WrestleMania 2000

		//96KB SRAM
		case fnv_hash("CDZ"): save_type = MemoryType::SRAM_96k; rpak = true; break; //Dezaemon 3D

		//128KB Flash
		case fnv_hash("NCC"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Command & Conquer
		case fnv_hash("NDA"): save_type = MemoryType::FLASH_128k; cpak = true; break; //Derby Stallion 64
		case fnv_hash("NAF"): save_type = MemoryType::FLASH_128k; cpak = true; rtc = true; break; //Doubutsu no Mori
		case fnv_hash("NJF"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Jet Force Gemini [Star Twins (J)]
		case fnv_hash("NKJ"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Ken Griffey Jr.'s Slugfest
		case fnv_hash("NZS"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Legend of Zelda: Majora's Mask [Zelda no Densetsu - Mujura no Kamen (J)]
		case fnv_hash("NM6"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Mega Man 64
		case fnv_hash("NCK"): save_type = MemoryType::FLASH_128k; rpak = true; break; //NBA Courtside 2 featuring Kobe Bryant
		case fnv_hash("NMQ"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Paper Mario
		case fnv_hash("NPN"): save_type = MemoryType::FLASH_128k; break; //Pokemon Puzzle League
		case fnv_hash("NPF"): save_type = MemoryType::FLASH_128k; break; //Pokemon Snap [Pocket Monsters Snap (J)]
		case fnv_hash("NPO"): save_type = MemoryType::FLASH_128k; tpak = true; break; //Pokemon Stadium
		case fnv_hash("CP2"): save_type = MemoryType::FLASH_128k; tpak = true; break; //Pocket Monsters Stadium 2 (J)
		case fnv_hash("NP3"): save_type = MemoryType::FLASH_128k; tpak = true; break; //Pokemon Stadium 2 [Pocket Monsters Stadium - Kin Gin (J)]
		case fnv_hash("NRH"): save_type = MemoryType::FLASH_128k; rpak = true; break; //Rockman Dash - Hagane no Boukenshin (J)
		case fnv_hash("NSQ"): save_type = MemoryType::FLASH_128k; rpak = true; break; //StarCraft 64
		case fnv_hash("NT9"): save_type = MemoryType::FLASH_128k; break; //Tigger's Honey Hunt
		case fnv_hash("NW4"): save_type = MemoryType::FLASH_128k; cpak = true; rpak = true; break; //WWF No Mercy
		case fnv_hash("NDP"): save_type = MemoryType::FLASH_128k; break; //Dinosaur Planet (Unlicensed)

		//Controller Pak
		case fnv_hash("NO7"): cpak = true; rpak = true; break; //The World Is Not Enough
		case fnv_hash("NAY"): cpak = true; break; //Aidyn Chronicles - The First Mage
		case fnv_hash("NBS"): cpak = true; rpak = true; break; //All-Star Baseball '99
		case fnv_hash("NBE"): cpak = true; rpak = true; break; //All-Star Baseball 2000
		case fnv_hash("NAS"): cpak = true; rpak = true; break; //All-Star Baseball 2001
		case fnv_hash("NAR"): cpak = true; rpak = true; break; //Armorines - Project S.W.A.R.M.
		case fnv_hash("NAC"): cpak = true; rpak = true; break; //Army Men - Air Combat
		case fnv_hash("NAM"): cpak = true; rpak = true; break; //Army Men - Sarge's Heroes
		case fnv_hash("N32"): cpak = true; rpak = true; break; //Army Men - Sarge's Heroes 2
		case fnv_hash("NAH"): cpak = true; rpak = true; break; //Asteroids Hyper 64
		case fnv_hash("NLC"): cpak = true; rpak = true; break; //Automobili Lamborghini [Super Speed Race 64 (J)]
		case fnv_hash("NBJ"): cpak = true; break; //Bakushou Jinsei 64 - Mezase! Resort Ou
		case fnv_hash("NB4"): cpak = true; rpak = true; break; //Bass Masters 2000
		case fnv_hash("NBX"): cpak = true; rpak = true; break; //Battletanx
		case fnv_hash("NBQ"): cpak = true; rpak = true; break; //Battletanx - Global Assault
		case fnv_hash("NZO"): cpak = true; rpak = true; break; //Battlezone - Rise of the Black Dogs
		case fnv_hash("NNS"): cpak = true; rpak = true; break; //Beetle Adventure Racing
		case fnv_hash("NB8"): cpak = true; rpak = true; break; //Beetle Adventure Racing (J)
		case fnv_hash("NBF"): cpak = true; rpak = true; break; //Bio F.R.E.A.K.S.
		case fnv_hash("NBP"): cpak = true; rpak = true; break; //Blues Brothers 2000
		case fnv_hash("NBO"): cpak = true; break; //Bottom of the 9th
		case fnv_hash("NOW"): cpak = true; break; //Brunswick Circuit Pro Bowling
		case fnv_hash("NBL"): cpak = true; rpak = true; break; //Buck Bumble
		case fnv_hash("NBY"): cpak = true; rpak = true; break; //Bug's Life, A
		case fnv_hash("NB3"): cpak = true; rpak = true; break; //Bust-A-Move '99 [Bust-A-Move 3 DX (E)]
		case fnv_hash("NBU"): cpak = true; break; //Bust-A-Move 2 - Arcade Edition
		case fnv_hash("NCL"): cpak = true; rpak = true; break; //California Speed
		case fnv_hash("NCD"): cpak = true; rpak = true; break; //Carmageddon 64
		case fnv_hash("NTS"): cpak = true; break; //Centre Court Tennis [Let's Smash (J)]
		case fnv_hash("NV2"): cpak = true; rpak = true; break; //Chameleon Twist 2
		case fnv_hash("NPK"): cpak = true; break; //Chou Kuukan Night Pro Yakyuu King (J)
		case fnv_hash("NT4"): cpak = true; rpak = true; break; //CyberTiger
		case fnv_hash("NDW"): cpak = true; rpak = true; break; //Daikatana, John Romero's
		case fnv_hash("NGA"): cpak = true; rpak = true; break; //Deadly Arts [G.A.S.P!! Fighter's NEXTream (E-J)]
		case fnv_hash("NDE"): cpak = true; rpak = true; break; //Destruction Derby 64
		case fnv_hash("NTA"): cpak = true; rpak = true; break; //Disney's Tarzan
		case fnv_hash("NDM"): cpak = true; break; //Doom 64
		case fnv_hash("NDH"): cpak = true; break; //Duel Heroes
		case fnv_hash("NDN"): cpak = true; rpak = true; break; //Duke Nukem 64
		case fnv_hash("NDZ"): cpak = true; rpak = true; break; //Duke Nukem - Zero Hour
		case fnv_hash("NWI"): cpak = true; rpak = true; break; //ECW Hardcore Revolution
		case fnv_hash("NST"): cpak = true; break; //Eikou no Saint Andrews
		case fnv_hash("NET"): cpak = true; break; //Quest 64 [Eltale Monsters (J) Holy Magic Century (E)]
		case fnv_hash("NEG"): cpak = true; rpak = true; break; //Extreme-G
		case fnv_hash("NG2"): cpak = true; rpak = true; break; //Extreme-G XG2
		case fnv_hash("NHG"): cpak = true; break; //F-1 Pole Position 64
		case fnv_hash("NFR"): cpak = true; rpak = true; break; //F-1 Racing Championship
		case fnv_hash("N8I"): cpak = true; break; //FIFA - Road to World Cup 98 [World Cup e no Michi (J)]
		case fnv_hash("N9F"): cpak = true; break; //FIFA 99
		case fnv_hash("N7I"): cpak = true; break; //FIFA Soccer 64 [FIFA 64 (E)]
		case fnv_hash("NFS"): cpak = true; break; //Famista 64
		case fnv_hash("NFF"): cpak = true; rpak = true; break; //Fighting Force 64
		case fnv_hash("NFD"): cpak = true; rpak = true; break; //Flying Dragon
		case fnv_hash("NFO"): cpak = true; rpak = true; break; //Forsaken 64
		case fnv_hash("NF9"): cpak = true; break; //Fox Sports College Hoops '99
		case fnv_hash("NG5"): cpak = true; rpak = true; break; //Ganbare Goemon - Neo Momoyama Bakufu no Odori [Mystical Ninja Starring Goemon]
		case fnv_hash("NGX"): cpak = true; rpak = true; break; //Gauntlet Legends
		case fnv_hash("NGD"): cpak = true; rpak = true; break; //Gauntlet Legends (J)
		case fnv_hash("NX3"): cpak = true; rpak = true; break; //Gex 3 - Deep Cover Gecko
		case fnv_hash("NX2"): cpak = true; break; //Gex 64 - Enter the Gecko
		case fnv_hash("NGM"): cpak = true; rpak = true; break; //Goemon's Great Adventure [Mystical Ninja 2 Starring Goemon]
		case fnv_hash("NGN"): cpak = true; break; //Golden Nugget 64
		case fnv_hash("NHS"): cpak = true; break; //Hamster Monogatari 64
		case fnv_hash("NM9"): cpak = true; break; //Harukanaru Augusta Masters 98
		case fnv_hash("NHC"): cpak = true; rpak = true; break; //Hercules - The Legendary Journeys
		case fnv_hash("NHX"): cpak = true; break; //Hexen
		case fnv_hash("NHK"): cpak = true; rpak = true; break; //Hiryuu no Ken Twin
		case fnv_hash("NHW"): cpak = true; rpak = true; break; //Hot Wheels Turbo Racing
		case fnv_hash("NHV"): cpak = true; rpak = true; break; //Hybrid Heaven (U + E)
		case fnv_hash("NHT"): cpak = true; rpak = true; break; //Hydro Thunder
		case fnv_hash("NWB"): cpak = true; rpak = true; break; //Iggy's Reckin' Balls [Iggy-kun no Bura Bura Poyon (J)]
		case fnv_hash("NWS"): cpak = true; break; //International Superstar Soccer '98 [Jikkyo World Soccer - World Cup France '98 (J)]
		case fnv_hash("NIS"): cpak = true; rpak = true; break; //International Superstar Soccer 2000 
		case fnv_hash("NJP"): cpak = true; break; //International Superstar Soccer 64 [Jikkyo J-League Perfect Striker (J)]
		case fnv_hash("NDS"): cpak = true; break; //J.League Dynamite Soccer 64
		case fnv_hash("NJE"): cpak = true; break; //J.League Eleven Beat 1997
		case fnv_hash("NJL"): cpak = true; break; //J.League Live 64
		case fnv_hash("NMA"): cpak = true; break; //Jangou Simulation Mahjong Do 64
		case fnv_hash("NCO"): cpak = true; rpak = true; break; //Jeremy McGrath Supercross 2000
		case fnv_hash("NGS"): cpak = true; break; //Jikkyou G1 Stable
		case fnv_hash("NJ3"): cpak = true; break; //Jikkyou World Soccer 3
		case fnv_hash("N64"): cpak = true; rpak = true; break; //Kira to Kaiketsu! 64 Tanteidan
		case fnv_hash("NKK"): cpak = true; rpak = true; break; //Knockout Kings 2000
		case fnv_hash("NLG"): cpak = true; rpak = true; break; //LEGO Racers
		case fnv_hash("N8M"): cpak = true; rpak = true; break; //Madden Football 64
		case fnv_hash("NMD"): cpak = true; rpak = true; break; //Madden Football 2000
		case fnv_hash("NFL"): cpak = true; rpak = true; break; //Madden Football 2001
		case fnv_hash("N2M"): cpak = true; rpak = true; break; //Madden Football 2002
		case fnv_hash("N9M"): cpak = true; rpak = true; break; //Madden Football '99
		case fnv_hash("NMJ"): cpak = true; break; //Mahjong 64
		case fnv_hash("NMM"): cpak = true; break; //Mahjong Master
		case fnv_hash("NHM"): cpak = true; rpak = true; break; //Mia Hamm Soccer 64
		case fnv_hash("NWK"): cpak = true; rpak = true; break; //Michael Owens WLS 2000 [World League Soccer 2000 (E) / Telefoot Soccer 2000 (F)]
		case fnv_hash("NV3"): cpak = true; rpak = true; break; //Micro Machines 64 Turbo
		case fnv_hash("NAI"): cpak = true; break; //Midway's Greatest Arcade Hits Volume 1
		case fnv_hash("NMB"): cpak = true; rpak = true; break; //Mike Piazza's Strike Zone
		case fnv_hash("NBR"): cpak = true; rpak = true; break; //Milo's Astro Lanes
		case fnv_hash("NM4"): cpak = true; rpak = true; break; //Mortal Kombat 4
		case fnv_hash("NMY"): cpak = true; rpak = true; break; //Mortal Kombat Mythologies - Sub-Zero
		case fnv_hash("NP9"): cpak = true; rpak = true; break; //Ms. Pac-Man - Maze Madness
		case fnv_hash("NH5"): cpak = true; break; //Nagano Winter Olympics '98 [Hyper Olympics in Nagano (J)]
		case fnv_hash("NNM"): cpak = true; break; //Namco Museum 64
		case fnv_hash("N9C"): cpak = true; rpak = true; break; //Nascar '99
		case fnv_hash("NN2"): cpak = true; rpak = true; break; //Nascar 2000
		case fnv_hash("NXG"): cpak = true; break; //NBA Hangtime
		case fnv_hash("NBA"): cpak = true; rpak = true; break; //NBA In the Zone '98 [NBA Pro '98 (E)]
		case fnv_hash("NB2"): cpak = true; rpak = true; break; //NBA In the Zone '99 [NBA Pro '99 (E)]
		case fnv_hash("NWZ"): cpak = true; rpak = true; break; //NBA In the Zone 2000
		case fnv_hash("NB9"): cpak = true; break; //NBA Jam '99
		case fnv_hash("NJA"): cpak = true; rpak = true; break; //NBA Jam 2000
		case fnv_hash("N9B"): cpak = true; rpak = true; break; //NBA Live '99
		case fnv_hash("NNL"): cpak = true; rpak = true; break; //NBA Live 2000
		case fnv_hash("NSO"): cpak = true; break; //NBA Showtime - NBA on NBC
		case fnv_hash("NBZ"): cpak = true; rpak = true; break; //NFL Blitz
		case fnv_hash("NSZ"): cpak = true; rpak = true; break; //NFL Blitz - Special Edition
		case fnv_hash("NBI"): cpak = true; rpak = true; break; //NFL Blitz 2000
		case fnv_hash("NFB"): cpak = true; rpak = true; break; //NFL Blitz 2001
		case fnv_hash("NQ8"): cpak = true; rpak = true; break; //NFL Quarterback Club '98
		case fnv_hash("NQ9"): cpak = true; rpak = true; break; //NFL Quarterback Club '99
		case fnv_hash("NQB"): cpak = true; rpak = true; break; //NFL Quarterback Club 2000
		case fnv_hash("NQC"): cpak = true; rpak = true; break; //NFL Quarterback Club 2001
		case fnv_hash("N9H"): cpak = true; rpak = true; break; //NHL '99
		case fnv_hash("NHO"): cpak = true; rpak = true; break; //NHL Blades of Steel '99 [NHL Pro '99 (E)]
		case fnv_hash("NHL"): cpak = true; rpak = true; break; //NHL Breakaway '98
		case fnv_hash("NH9"): cpak = true; rpak = true; break; //NHL Breakaway '99
		case fnv_hash("NNC"): cpak = true; rpak = true; break; //Nightmare Creatures
		case fnv_hash("NCE"): cpak = true; rpak = true; break; //Nuclear Strike 64
		case fnv_hash("NOF"): cpak = true; rpak = true; break; //Offroad Challenge
		case fnv_hash("NHN"): cpak = true; break; //Olympic Hockey Nagano '98
		case fnv_hash("NOM"): cpak = true; break; //Onegai Monsters
		case fnv_hash("NPC"): cpak = true; break; //Pachinko 365 Nichi (J)
		case fnv_hash("NYP"): cpak = true; rpak = true; break; //Paperboy
		case fnv_hash("NPX"): cpak = true; rpak = true; break; //Polaris SnoCross
		case fnv_hash("NPL"): cpak = true; break; //Power League 64 (J)
		case fnv_hash("NPU"): cpak = true; break; //Power Rangers - Lightspeed Rescue
		case fnv_hash("NKM"): cpak = true; break; //Pro Mahjong Kiwame 64 (J)
		case fnv_hash("NNR"): cpak = true; break; //Pro Mahjong Tsuwamono 64 - Jansou Battle ni Chousen (J)
		case fnv_hash("NPB"): cpak = true; rpak = true; break; //Puzzle Bobble 64 (J)
		case fnv_hash("NQK"): cpak = true; rpak = true; break; //Quake 64
		case fnv_hash("NQ2"): cpak = true; rpak = true; break; //Quake 2
		case fnv_hash("NKR"): cpak = true; break; //Rakuga Kids (E)
		case fnv_hash("NRP"): cpak = true; rpak = true; break; //Rampage - World Tour
		case fnv_hash("NRT"): cpak = true; break; //Rat Attack
		case fnv_hash("NRX"): cpak = true; break; //Robotron 64
		case fnv_hash("NY2"): cpak = true; break; //Rayman 2 - The Great Escape
		case fnv_hash("NFQ"): cpak = true; rpak = true; break; //Razor Freestyle Scooter
		case fnv_hash("NRV"): cpak = true; rpak = true; break; //Re-Volt
		case fnv_hash("NRD"): cpak = true; rpak = true; break; //Ready 2 Rumble Boxing
		case fnv_hash("N22"): cpak = true; rpak = true; break; //Ready 2 Rumble Boxing - Round 2
		case fnv_hash("NRO"): cpak = true; rpak = true; break; //Road Rash 64
		case fnv_hash("NRR"): cpak = true; rpak = true; break; //Roadster's Trophy
		case fnv_hash("NRK"): cpak = true; break; //Rugrats in Paris - The Movie
		case fnv_hash("NR2"): cpak = true; rpak = true; break; //Rush 2 - Extreme Racing USA
		case fnv_hash("NCS"): cpak = true; rpak = true; break; //S.C.A.R.S.
		case fnv_hash("NDC"): cpak = true; rpak = true; break; //SD Hiryuu no Ken Densetsu (J)
		case fnv_hash("NSH"): cpak = true; break; //Saikyou Habu Shougi (J)
		case fnv_hash("NSF"): cpak = true; rpak = true; break; //San Francisco Rush - Extreme Racing
		case fnv_hash("NRU"): cpak = true; rpak = true; break; //San Francisco Rush 2049
		case fnv_hash("NSY"): cpak = true; break; //Scooby-Doo! - Classic Creep Capers
		case fnv_hash("NSD"): cpak = true; rpak = true; break; //Shadow Man
		case fnv_hash("NSG"): cpak = true; break; //Shadowgate 64 - Trials Of The Four Towers
		case fnv_hash("NTO"): cpak = true; break; //Shin Nihon Pro Wrestling - Toukon Road - Brave Spirits (J)
		case fnv_hash("NS2"): cpak = true; break; //Simcity 2000
		case fnv_hash("NSK"): cpak = true; rpak = true; break; //Snowboard Kids [Snobow Kids (J)]
		case fnv_hash("NDT"): cpak = true; rpak = true; break; //South Park
		case fnv_hash("NPR"): cpak = true; rpak = true; break; //South Park Rally
		case fnv_hash("NIV"): cpak = true; rpak = true; break; //Space Invaders
		case fnv_hash("NSL"): cpak = true; rpak = true; break; //Spider-Man
		case fnv_hash("NR3"): cpak = true; rpak = true; break; //Stunt Racer 64
		case fnv_hash("NBW"): cpak = true; rpak = true; break; //Super Bowling
		case fnv_hash("NSX"): cpak = true; rpak = true; break; //Supercross 2000
		case fnv_hash("NSP"): cpak = true; rpak = true; break; //Superman
		case fnv_hash("NPZ"): cpak = true; rpak = true; break; //Susume! Taisen Puzzle Dama Toukon! Marumata Chou (J)
		case fnv_hash("NL2"): cpak = true; rpak = true; break; //Top Gear Rally 2
		case fnv_hash("NR6"): cpak = true; rpak = true; break; //Tom Clancy's Rainbow Six
		case fnv_hash("NTT"): cpak = true; break; //Tonic Trouble
		case fnv_hash("NTF"): cpak = true; rpak = true; break; //Tony Hawk's Pro Skater
		case fnv_hash("NTQ"): cpak = true; rpak = true; break; //Tony Hawk's Pro Skater 2
		case fnv_hash("N3T"): cpak = true; rpak = true; break; //Tony Hawk's Pro Skater 3
		case fnv_hash("NGB"): cpak = true; rpak = true; break; //Top Gear Hyper Bike
		case fnv_hash("NGR"): cpak = true; rpak = true; break; //Top Gear Rally (U)
		case fnv_hash("NTH"): cpak = true; rpak = true; break; //Toy Story 2 - Buzz Lightyear to the Rescue!
		case fnv_hash("N3P"): cpak = true; rpak = true; break; //Triple Play 2000
		case fnv_hash("NTU"): cpak = true; break; //Turok: Dinosaur Hunter [Turok: Jikuu Senshi (J)]
		case fnv_hash("NRW"): cpak = true; rpak = true; break; //Turok: Rage Wars
		case fnv_hash("NT2"): cpak = true; rpak = true; break; //Turok 2 - Seeds of Evil [Violence Killer - Turok New Generation (J)]
		case fnv_hash("NTK"): cpak = true; rpak = true; break; //Turok 3 - Shadow of Oblivion
		case fnv_hash("NSB"): cpak = true; rpak = true; break; //Twisted Edge - Extreme Snowboarding [King Hill 64 - Extreme Snowboarding (J)]
		case fnv_hash("NV8"): cpak = true; rpak = true; break; //Vigilante 8
		case fnv_hash("NVG"): cpak = true; rpak = true; break; //Vigilante 8 - Second Offense
		case fnv_hash("NVC"): cpak = true; break; //Virtual Chess 64
		case fnv_hash("NVR"): cpak = true; break; //Virtual Pool 64
		case fnv_hash("NWV"): cpak = true; rpak = true; break; //WCW: Backstage Assault
		case fnv_hash("NWM"): cpak = true; rpak = true; break; //WCW: Mayhem
		case fnv_hash("NW3"): cpak = true; rpak = true; break; //WCW: Nitro
		case fnv_hash("NWN"): cpak = true; rpak = true; break; //WCW vs. nWo - World Tour
		case fnv_hash("NWW"): cpak = true; rpak = true; break; //WWF: War Zone
		case fnv_hash("NTI"): cpak = true; rpak = true; break; //WWF: Attitude
		case fnv_hash("NWG"): cpak = true; break; //Wayne Gretzky's 3D Hockey
		case fnv_hash("NW8"): cpak = true; break; //Wayne Gretzky's 3D Hockey '98
		case fnv_hash("NWD"): cpak = true; rpak = true; break; //Winback - Covert Operations
		case fnv_hash("NWP"): cpak = true; rpak = true; break; //Wipeout 64
		case fnv_hash("NJ2"): cpak = true; break; //Wonder Project J2 - Koruro no Mori no Jozet (J)
		case fnv_hash("N8W"): cpak = true; break; //World Cup '98
		case fnv_hash("NWO"): cpak = true; rpak = true; break; //World Driver Championship
		case fnv_hash("NXF"): cpak = true; rpak = true; break; //Xena Warrior Princess - The Talisman of Fate

		//Rumble Pak
		case fnv_hash("NJQ"): rpak = true; break; //Batman Beyond - Return of the Joker [Batman of the Future - Return of the Joker (E)]
		case fnv_hash("NCB"): rpak = true; break; //Charlie Blast's Territory
		case fnv_hash("NDF"): rpak = true; break; //Dance Dance Revolution - Disney Dancing Museum
		case fnv_hash("NKE"): rpak = true; break; //Knife Edge - Nose Gunner
		case fnv_hash("NMT"): rpak = true; break; //Magical Tetris Challenge
		case fnv_hash("NM3"): rpak = true; break; //Monster Truck Madness 64
		case fnv_hash("NRG"): rpak = true; break; //Rugrats - Scavenger Hunt [Treasure Hunt (E)]
		case fnv_hash("NOH"): rpak = true; tpak = true; break; //Transformers Beast Wars - Transmetals
		case fnv_hash("NWF"): rpak = true; break; //Wheel of Fortune

		//Special case for save type in International Track & Field
		case fnv_hash("N3H"):
			if (region_code == 'J') {
				save_type = MemoryType::SRAM_32k; //Ganbare! Nippon! Olympics 2000
			}
			else {
				cpak = true; //International Track & Field 2000|Summer Games
				rpak = true;
			}
			break;

		//Special cases for Japanese versions of Castlevania
		case fnv_hash("ND3"):
			if (region_code == 'J') {
				save_type = MemoryType::EEPROM_2k; //Akumajou Dracula Mokushiroku (J)
				rpak = true;
			}
			else {
				cpak = true; break; //Castlevania
			}
			break;

		case fnv_hash("ND4"):
			if (region_code == 'J') {
				rpak = true; //Akumajou Dracula Mokushiroku Gaiden: Legend of Cornell (J)
			}
			else {
				cpak = true; //Castlevania - Legacy of Darkness
			}
			break;

		//Special case for Super Mario 64 Shindou Edition   
		case fnv_hash("NSM"):
			if (region_code == 'J' && revision == 3) {
				rpak = true;
			}
			save_type = MemoryType::EEPROM_512;
			break;

		//Special case for Wave Race 64 Shindou Edition
		case fnv_hash("NWR"):
			if (region_code == 'J' && revision == 2) {
				rpak = true; 
			}
			save_type = MemoryType::EEPROM_512;
			cpak = true;
			break;

		//Special case for save type in Kirby 64: The Crystal Shards [Hoshi no Kirby 64 (J)]
		case fnv_hash("NK4"):
			if (region_code == 'J' && revision < 2) {
				save_type = MemoryType::SRAM_32k; 
			}
			else { 
				save_type = MemoryType::EEPROM_2k;
			}
			rpak = true;
			break;

		//Special case for save type in Dark Rift [Space Dynamites (J)]
		case fnv_hash("NDK"):
			if (region_code == 'J') {
				save_type = MemoryType::EEPROM_512;
			}
			break;

		//Special case for save type in Wetrix
		case fnv_hash("NWT"):
			if (region_code == 'J') {
				save_type = MemoryType::EEPROM_512; 
			} else { 
				cpak = true; 
			}
			break;

		//Unknown ROM
		default: return 2;
	};

	user_io_status_set("[71]", (uint32_t)cpak);
	user_io_status_set("[72]", (uint32_t)rpak);
	user_io_status_set("[73]", (uint32_t)tpak);
	user_io_status_set("[74]", (uint32_t)rtc);
	user_io_status_set("[77:75]", (uint32_t)save_type);

	return 0;
}

static void md5_to_hex(uint8_t* in, char* out)
{
	char* p = out;
	for (int i = 0; i < 16; i++)
	{
		sprintf(p, "%02x", in[i]);
		p += 2;
	}
	*p = '\0';
}

int n64_rom_tx(const char* name, unsigned char index)
{
	static uint8_t buf[4096];
	fileTYPE f = {};

	if (!FileOpen(&f, name, 1)) return 0;

	unsigned long bytes2send = f.size;

	printf("N64 file %s with %lu bytes to send for index %04X\n", name, bytes2send, index);

	// set index byte
	user_io_set_index(index);

	// prepare transmission of new file
	user_io_set_download(1);

	int use_progress = 1;
	int size = bytes2send;
	if (use_progress) ProgressMessage(0, 0, 0, 0);

	// save state processing
	process_ss(name);

	bool is_first_chunk = true;
	bool rom_found_in_db = false;
	uint8_t detection_error = 0;
	RomFormat rom_format = RomFormat::UNKNOWN;

	MD5Context ctx;
	MD5Init(&ctx);
	uint8_t md5[16];
	char md5_hex[40];
	uint64_t ipl3_crc = 0;
	char cart_id[4];
	char region_code = '\0';
	uint8_t revision = 0;

	while (bytes2send)
	{
		uint32_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);

		// perform sanity checks and detect ROM format
		if (is_first_chunk)
		{
			if (chunk < 4096)
			{
				printf("Failed to load ROM: must be at least 4096 bytes\n");
				return 0;
			}
			rom_format = detectRomFormat(buf);
		}

		// normalize data to big-endian format
		normalizeData(buf, chunk, rom_format);

		MD5Update(&ctx, buf, chunk);

		if (is_first_chunk)
		{
			// try to detect ROM settings based on header MD5 hash

			// For calculating the MD5 hash of the header, we need to make a
			// copy of the context before calling MD5Final, otherwise the file
			// hash will be incorrect lateron.
			MD5Context ctx_header;
			memcpy(&ctx_header, &ctx, sizeof(struct MD5Context));
			MD5Final(md5, &ctx_header);
			md5_to_hex(md5, md5_hex);
			printf("Header MD5: %s\n", md5_hex);

			rom_found_in_db = detect_rom_settings_in_dbs(md5_hex);
			if (!rom_found_in_db)
			{
				printf("No ROM information found for header hash: %s\n", md5_hex);
				for (size_t i = 0x40 / sizeof(uint32_t); i < 0x1000 / sizeof(uint32_t); i++) ipl3_crc += ((uint32_t*)buf)[i];
				strncpy(cart_id, (char*)(buf + 0x3b), 3);
				cart_id[3] = '\0';
				region_code = buf[0x3e];
				revision = buf[0x3f];
			}
		}

		user_io_file_tx_data(buf, chunk);

		if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
		bytes2send -= chunk;
		is_first_chunk = false;
	}

	MD5Final(md5, &ctx);
	md5_to_hex(md5, md5_hex);
	printf("File MD5: %s\n", md5_hex);

	// Try to detect ROM settings from file MD5 if they are not available yet
	if (!rom_found_in_db)
	{
		rom_found_in_db = detect_rom_settings_in_dbs(md5_hex);
		if (!rom_found_in_db) printf("No ROM information found for file hash: %s\n", md5_hex);
	}

	// Try detect ROM settings by analyzing the ROM itself. (region, cic and save type)
	// Fallback for missing db entries.
	if (!rom_found_in_db)
	{
		detection_error = detect_rom_settings_from_first_chunk(cart_id, region_code, revision, ipl3_crc);
		if (detection_error == 1) {
			printf("Unknown CIC type: %016" PRIX64 "\n", ipl3_crc);
		}
		else if (detection_error == 2) {
			printf("Unknown Cart ID: %s\n", cart_id);
		}
	}

	printf("Done.\n");
	FileClose(&f);

	// mount save state
	char file_path[1024];
	FileGenerateSavePath(name, file_path);
	user_io_file_mount(file_path, 0, 1);

	// signal end of transmission
	user_io_set_download(0);
	ProgressMessage(0, 0, 0, 0);

	if (!rom_found_in_db)
	{
		if (detection_error == 1) {
			Info("Auto-detect failed:\nUnknown CIC type.\nN64-database.txt needed?");
		}
		else if (detection_error == 2) {
			Info("Auto-detect failed:\nUnknown Cart ID,\nSave type not determined.\nN64-database.txt needed?");
		}
	}

	return 1;
}
