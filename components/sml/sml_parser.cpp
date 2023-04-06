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

// Full decoding compliant to "FNN Lastenheft SMGW, Funktionale Merkmale Juni 2014"
std::string bytes_to_serverid(const bytes &buffer)
{
  std::string repr;
  int sz = buffer.size();
  if (sz < 1)
    return "EMPTY";
  uint8_t Byte0 = buffer[0];

  // 0x03: Rhein-Energie: 18 BCD digits
  if ((0x04 == Byte0) && (10 == sz))
  {
    uint64_t temp = 0;
    for (int i = 1; i <= 9; i++)
      repr += str_sprintf("%02x", buffer[i]);
    return repr;
  }

  // 0x04: EON: 16-decimal digits
  if ((0x04 == Byte0) && (8 == sz))
  {
    uint64_t temp = 0;
    for (int i = 1; i <= 7; i++)
      temp = (temp << 8) | (uint64_t)buffer[i];
    repr += str_sprintf("%016llu", temp);
    return repr;
  }

  // 0x05: MAC-Address
  if ((0x05 == Byte0) && (7 == sz))
  {
    uint64_t temp = 0;
    for (int i = 1; i <= 5; i++)
      repr += str_sprintf("%02x:", buffer[i]);
    repr += str_sprintf("%02x:", buffer[6]);
    return repr;
  }

  // E DIN 43863-5 rev. 02/2010
  if ((0x06 == Byte0) && (10 == sz))
  {
    uint64_t temp = 0;
    for (int i = 4; i <= 9; i++)
      temp = (temp << 8) | (uint64_t)buffer[i];
    // Manufacturer ID (https://www.dlms.com/flag-id/flag-id-list)
    uint8_t manufacturer_id[] = {buffer[1], buffer[2], buffer[3], 0};
    uint8_t meter_type = ((uint8_t)(temp / 1000000000000)) & 0x0f;
    temp = temp % 1000000000000;
    uint16_t fabrication_block = (uint16_t)(temp / 100000000);
    uint32_t fabrication_number = (uint32_t)(temp % 100000000);
    repr += str_sprintf("%01X%s%04u%08lu", meter_type, manufacturer_id, fabrication_block, fabrication_number);
    return repr;
  }

  // 0x07: IMEI: 15-decimal digits
  if ((0x07 == Byte0) && (8 == sz))
  {
    uint64_t temp = 0;
    for (int i = 1; i <= 7; i++)
      temp = (temp << 8) | (uint64_t)buffer[i];
    repr += str_sprintf("%015llu", temp);
    return repr;
  }

  // 0x08: RWE: 14 digits in format xxxxxx-yyyyyyy
  if ((0x08 == Byte0) && (8 == sz) && (45 == buffer[4]))
  {
    uint32_t xxxxxx = 0;
    uint32_t yyyyyyy = 0;

    for (int i = 1; i <= 3; i++)
      xxxxxx = (xxxxxx << 8) | (uint32_t)buffer[i];
    for (int i = 5; i <= 7; i++)
      yyyyyyy = (yyyyyyy << 8) | (uint32_t)buffer[i];

    repr += str_sprintf("%06u-%07u", xxxxxx, yyyyyyy);
    return repr;
  }

  // E DIN 43863-5 rev. 07/2010 or rev. 04/2012
  if (((0x09 == Byte0) || (0x0a == Byte0)) && (10 == sz))
  {
    // Type of Meter (1=electricity)
    uint8_t meter_type = buffer[1] & 0x0f;
    // Manufacturer ID (https://www.dlms.com/flag-id/flag-id-list)
    uint8_t manufacturer_id[] = {buffer[2], buffer[3], buffer[4], 0};
    // Fabrication block (hex)
    uint8_t fabrication_block = buffer[5];
    // Fabrication number (decimal)
    uint32_t fabrication_number = (((uint32_t)buffer[6]) << 24) | (((uint32_t)buffer[7]) << 16) | (((uint32_t)buffer[8]) << 8) | (uint32_t)buffer[9];
    fabrication_number %= 100000000;
    repr += str_sprintf("%01X%s%02X%08lu", meter_type, manufacturer_id, fabrication_block, fabrication_number); // e.g. 1ABC0012345678 spaces removed
    return repr;
  }

  repr += str_sprintf("(type=%02x len=%i) ", Byte0, sz);
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
