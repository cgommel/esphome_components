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

std::string unit_repr(const uint8_t unit)
{
  // source : https://www.dlms.com/files/Blue-Book-Ed-122-Excerpt.pdf / Table 4
  switch (unit)
  {
  case 1:
    return ("a");
  case 2:
    return ("mo");
  case 3:
    return ("wk");
  case 4:
    return ("d");
  case 5:
    return ("h");
  case 6:
    return ("min");
  case 7:
    return ("s");
  case 8:
    return ("°");
  case 9:
    return ("°C");
  case 10:
    return ("currency");
  case 11:
    return ("m");
  case 12:
    return ("m/s");
  case 13:
    return ("m^3");
  case 14:
    return ("m^3");
  case 15:
    return ("m^3/h");
  case 16:
    return ("m^3/d");
  case 17:
    return ("m^3/d");
  case 18:
    return ("m^3/d");
  case 19:
    return ("l");
  case 20:
    return ("kg");
  case 21:
    return ("N");
  case 22:
    return ("Nm");
  case 23:
    return ("Pa");
  case 24:
    return ("bar");
  case 25:
    return ("J");
  case 26:
    return ("J/h");
  case 27:
    return ("W");
  case 28:
    return ("Va");
  case 29:
    return ("var");
  case 30:
    return ("Wh");
  case 31:
    return ("VAh");
  case 32:
    return ("varh");
  case 33:
    return ("A");
  case 34:
    return ("C");
  case 35:
    return ("V");
  case 36:
    return ("V/m");
  case 37:
    return ("F");
  case 38:
    return ("Ohm");
  case 39:
    return ("Ohm*m^2/m");
  case 40:
    return ("Wb");
  case 41:
    return ("T");
  case 42:
    return ("A/m");
  case 43:
    return ("H");
  case 44:
    return ("Hz");
  case 45:
    return ("1/(Wh)");
  case 46:
    return ("1/(varh)");
  case 47:
    return ("1/(VAh)");
  case 48:
    return ("V^2*h");
  case 49:
    return ("A^2*h");
  case 50:
    return ("kg/s");
  case 51:
    return ("S"); // a.k.a. mho
  case 52:
    return ("K");
  case 53:
    return ("1/(V^2*h)");
  case 54:
    return ("1/(A^2*h)");
  case 55:
    return ("1/m^3");
  case 56:
    return ("%");
  case 57:
    return ("Ah");
  case 60:
    return ("Wh/m^3");
  case 61:
    return ("J/m^3");
  case 62:
    return ("Mol %");
  case 63:
    return ("g/m^3");
  case 64:
    return ("Pa*s");
  case 65:
    return ("J/kg");
  case 70:
    return ("dBm");
  case 71:
    return ("dBuV");
  case 72:
    return ("dB");

  case 253:
    return ("(reserved)");
  case 254:
    return ("(other)");
  case 255:
    return ("");
  default:
    return str_sprintf("(Unit %u)", unit);
  }
}

std::string bytes_to_serverid(const bytes &buffer) {
  std::string repr;
  int sz = buffer.size();
  //Modern Meters use this format
  if ((10 == sz) && (buffer.at(0)>=0x09)) {
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
