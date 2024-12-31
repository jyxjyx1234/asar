#include <iostream>
#include <fstream>
#include "json/json.h"
#include <filesystem>
#pragma comment(lib, "jsoncpp.lib")

void help() {
	std::cout << "Usage: asar <mode> <input> <output>" << std::endl;
	std::cout << "Example: asar unpack app.asar app" << std::endl;
	std::cout << "Example: asar pack app app.asar" << std::endl;
}

struct AsarHead {
	unsigned int magic;
	unsigned int unk1;
	unsigned int unk2;
	unsigned int json_size;
};

Json::Value read_json_from_string(std::string json_str) {
	Json::CharReaderBuilder ReaderBuilder;
	ReaderBuilder["emitUTF8"] = true;
	Json::Value jsonData;
	std::string errs;
	std::istringstream s(json_str);
	if (!Json::parseFromStream(ReaderBuilder, s, &jsonData, &errs)) {
		std::cerr << "Failed to parse JSON: " << errs << std::endl;
	}
	return jsonData;
}

void dump_file(std::fstream& packfile, int jsonsize, int size, int offset, std::string global_outpath, std::string outpath) {
    packfile.seekg(offset + jsonsize, std::ios::beg);
    char* buffer = new char[size];
    packfile.read(buffer, size);

	//std::cout << outpath << std::endl;
    std::filesystem::path out_path = std::filesystem::u8path(global_outpath + "/" + outpath);
    std::filesystem::create_directories(out_path.parent_path());

    std::ofstream outfile(out_path, std::ios::out | std::ios::binary);
    if (!outfile) {
        std::cerr << "Failed to open output file." << std::endl;
        delete[] buffer;
        return;
    }
    outfile.write(buffer, size);
    outfile.close();
    delete[] buffer;
}

void read_file_info(Json::Value data, std::fstream& packfile, int jsonsize, std::string global_outpath, std::string outpath) {
	for (const auto& key : data.getMemberNames()) {
		if (key == "files") {//文件夹
			const Json::Value& value = data[key];
			read_file_info(value, packfile, jsonsize, global_outpath, outpath);
		}
		else if (key == "offset" || key == "size") {//最后一级
            int size = data["size"].asInt();
            int offset = std::stoi(data["offset"].asString());
			dump_file(packfile, jsonsize, size, offset, global_outpath, outpath);
			return;
		}
		else {
			const Json::Value& value = data[key];
			read_file_info(value, packfile, jsonsize, global_outpath, outpath + "/" + key);
		}
	}
}

void unpack(std::string input, std::string output) {
	std::fstream file;
	file.open(input, std::ios::in | std::ios::binary);
	if (!file) {
		std::cerr << "Failed to open input file." << std::endl;
		return;
	}
	AsarHead head;
	file.read(reinterpret_cast<char*>(&head), sizeof(AsarHead));
	char* files_info = new char[head.json_size];
	file.read(files_info, head.json_size);
	std::string json_str(files_info);
	delete[] files_info;
	Json::Value file_info_json = read_json_from_string(json_str);

	read_file_info(file_info_json, file, head.unk2 + 12, output, "");
}

int offset;
Json::Value make_json(const std::string directory) {
	Json::Value folder_info;
	for (const auto& entry : std::filesystem::directory_iterator(directory)) {
		if (entry.is_directory()) {
			Json::Value folder_data_;
			Json::Value folder_data;
			folder_data = make_json(entry.path().u8string());
			folder_data_["files"] = folder_data;
			folder_info[entry.path().filename().u8string()] = folder_data_;
		}
		else {
			Json::Value file_info;
            file_info["offset"] = std::to_string(offset);
			file_info["size"] = std::filesystem::file_size(entry.path());
			offset = offset + std::filesystem::file_size(entry.path());
			folder_info[entry.path().filename().u8string()] = file_info;
		}
	}
	return folder_info;
}

void write_to_pack(std::string directory, std::ofstream& out_buffer) {
	for (const auto& entry : std::filesystem::directory_iterator(directory)) {
		if (entry.is_directory()) {
			write_to_pack(entry.path().u8string(), out_buffer);
		}
		else {
			std::ifstream infile(entry.path(), std::ios::binary);
			out_buffer << infile.rdbuf();
			infile.close();
		}
	}
}

void pack(std::string input, std::string output) {
	if (!std::filesystem::exists(input)) {
		std::cerr << "Input directory does not exist." << std::endl;
		return;
	}
	Json::Value file_info_ = make_json(input);
	Json::Value file_info;
	file_info["files"] = file_info_;

	std::ofstream outfile(output, std::ios::out | std::ios::binary);
	if (!outfile) {
		std::cerr << "Failed to open output file." << std::endl;
		return;
	}

	Json::StreamWriterBuilder writer;
    writer["emitUTF8"] = true;
    writer["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
    std::ostringstream os;
    jsonWriter->write(file_info, &os);

    std::string file_info_str = os.str();
	AsarHead head;
	head.magic = 4;
	head.json_size = file_info_str.size();
    int padding_size = 4 - head.json_size % 4;
    head.unk2 = head.json_size + padding_size + 4;
	head.unk1 = head.unk2 + 4;

    outfile.write(reinterpret_cast<char*>(&head), sizeof(AsarHead));
    outfile.write(file_info_str.c_str(), file_info_str.size());
    for (int i = 0; i < padding_size; ++i) {
        outfile.put('\0');
    }
	write_to_pack(input, outfile);
	outfile.close();
}

int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		help();
		return 1;
	}
	std::string mode = argv[1];
	std::string input = argv[2];
	std::string output = argv[3];
	if (mode == "pack") {
		pack(input, output);
	}
	else if (mode == "unpack") {
		unpack(input, output);
	}
	else if (mode == "remove") {
		std::filesystem::remove_all(input);
	}
	else {
		help();
	}
    return 0;
}
