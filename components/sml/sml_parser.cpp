#include "esphome/core/helpers.h"
#include "constants.h"
#include "sml_parser.h"

namespace esphome {
namespace sml {

SmlFile::SmlFile(bytes buffer) : buffer_(std::move(buffer)) {
  // extract messages
  this->pos_ = 0;
  while (this->pos_ < this->buffer_.size()) {
    if (this->buffer_[this->pos_] == 0x00)
      break;  // fill byte detected -> no more messages

    SmlNode message = SmlNode();
    if (!this->setup_node(&message))
      break;
    this->messages.emplace_back(message);
  }
}

bool SmlFile::setup_node(SmlNode *node) {
  uint8_t type = this->buffer_[this->pos_] >> 4;      // type including overlength info
  uint8_t length = this->buffer_[this->pos_] & 0x0f;  // length including TL bytes
  bool is_list = (type & 0x07) == SML_LIST;
  bool has_extended_length = type & 0x08;  // we have a long list/value (>15 entries)
  uint8_t parse_length = length;
  if (has_extended_length) {
    length = (length << 4) + (this->buffer_[this->pos_ + 1] & 0x0f);
    parse_length = length - 1;
    this->pos_ += 1;
  }

  if (this->pos_ + parse_length >= this->buffer_.size())
    return false;

  node->type = type & 0x07;
  node->nodes.clear();
  node->value_bytes.clear();
  if (this->buffer_[this->pos_] == 0x00) {  // end of message
    this->pos_ += 1;
  } else if (is_list) {  // list
    this->pos_ += 1;
    node->nodes.reserve(parse_length);
    for (size_t i = 0; i != parse_length; i++) {
      SmlNode child_node = SmlNode();
      if (!this->setup_node(&child_node))
        return false;
      node->nodes.emplace_back(child_node);
    }
  } else {  // value
    node->value_bytes =
        bytes(this->buffer_.begin() + this->pos_ + 1, this->buffer_.begin() + this->pos_ + parse_length);
    this->pos_ += parse_length;
  }
  return true;
}

std::vector<ObisInfo> SmlFile::get_obis_info() {
  std::vector<ObisInfo> obis_info;
  for (auto const &message : messages) {
    SmlNode message_body = message.nodes[3];
    uint16_t message_type = bytes_to_uint(message_body.nodes[0].value_bytes);
    if (message_type != SML_GET_LIST_RES)
      continue;

    SmlNode get_list_response = message_body.nodes[1];
    bytes server_id = get_list_response.nodes[1].value_bytes;
    SmlNode val_list = get_list_response.nodes[4];

    for (auto const &val_list_entry : val_list.nodes) {
      obis_info.emplace_back(server_id, val_list_entry);
    }
  }
  return obis_info;
}

std::string bytes_repr(const bytes &buffer) {
  std::string repr;
  int sz=buffer.size();
  for (auto const value : buffer) {
    repr += str_sprintf("%02x", value & 0xff);
  }
  return repr;
}

std::string bytes_to_serverid(const bytes &buffer) {
  std::string repr;
  int sz = buffer.size();
  //Modern Meters use this format
  if ((10 == sz) && (buffer.at(0)==0x0a)) {
    // Type of Meter (1=electricity)
    uint8_t meter_type = buffer.at(1) % 10;
    // Manufacturer ID (https://www.dlms.com/flag-id/flag-id-list)
    uint8_t manufacturer_id[] = {buffer.at(2), buffer.at(3), buffer.at(4), 0};
    // Fabrication block (hex)
    uint8_t fabrication_block = buffer.at(5);
    // Fabrication number (decimal)
    uint32_t fabrication_number = (((uint32_t)buffer.at(6)) << 24) | (((uint32_t)buffer.at(7)) << 16) | (((uint32_t)buffer.at(8)) << 8) | (uint32_t)buffer.at(9);
    fabrication_number %= 100000000;
    repr += str_sprintf("%u%s%02X%08zu", meter_type, manufacturer_id, fabrication_block, fabrication_number); // e.g. 1ABC0012345678 spaces removed
    return repr;
  }
  //Old EDL21 meters send it a little bit different
  if ((10 == sz) && (buffer.at(0)==0x06)) {
    uint64_t temp = (((uint64_t)buffer[4]) << 40) | (((uint64_t)buffer[5]) << 32) | (((uint64_t)buffer[6]) << 24) | (((uint64_t)buffer[7]) << 16) | (((uint64_t)buffer[8]) << 8) | (uint64_t)buffer[9];
    char tempStr[16];
    sprintf(tempStr, "%llu", temp);
    uint8_t pos = 5;
    while (tempStr[pos] == '0')
      pos++;
    repr += str_sprintf( "%c%c%c%c%c%c%c%c%s", tempStr[0], buffer[1], buffer[2], buffer[3], tempStr[1], tempStr[2], tempStr[3], tempStr[4], tempStr + pos);
    return repr;
  }
  

  repr += str_sprintf("(len=%i) ", sz);
  for (auto const value : buffer)
  {
    repr += str_sprintf("%02x", value & 0xff);
  }
  return repr;
 
}

uint64_t bytes_to_uint(const bytes &buffer) {
  uint64_t val = 0;
  for (auto const value : buffer) {
    val = (val << 8) + value;
  }
  return val;
}

int64_t bytes_to_int(const bytes &buffer) {
  uint64_t tmp = bytes_to_uint(buffer);
  int64_t val;

  switch (buffer.size()) {
    case 1:  // int8
      val = (int8_t) tmp;
      break;
    case 2:  // int16
      val = (int16_t) tmp;
      break;
    case 4:  // int32
      val = (int32_t) tmp;
      break;
    default:  // int64
      val = (int64_t) tmp;
  }
  return val;
}

std::string bytes_to_string(const bytes &buffer) { return std::string(buffer.begin(), buffer.end()); }

ObisInfo::ObisInfo(bytes server_id, SmlNode val_list_entry) : server_id(std::move(server_id)) {
  this->code = val_list_entry.nodes[0].value_bytes;
  this->status = val_list_entry.nodes[1].value_bytes;
  this->unit = bytes_to_uint(val_list_entry.nodes[3].value_bytes);
  this->scaler = bytes_to_int(val_list_entry.nodes[4].value_bytes);
  SmlNode value_node = val_list_entry.nodes[5];
  this->value = value_node.value_bytes;
  this->value_type = value_node.type;
}

std::string ObisInfo::code_repr() const {
  return str_sprintf("%d-%d:%d.%d.%d*%d", this->code[0], this->code[1], this->code[2], this->code[3], this->code[4], this->code[5]);
}

}  // namespace sml
}  // namespace esphome
