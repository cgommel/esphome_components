# SML component for ESPHome

## About
This external component provides a way to retrieve data from devices using the *Smart Meter Language* (SML). SML is mainly used by electrical counters but also solar devices, gas meters, and much more.

Although the SML protocol is well defined, it gives a lot of freedom to the manufacturers how to store and identify the transmitted data.
A transmitted physical value is identified by an OBIS code (Object Identification System). If it is known which code the manufacturer assigns to the physical value, the corresponding value can be extracted. Unfortunately this differs from manufacturer to manufacturer. Also the transmitted physical values are not fixed.

As an example, many manufacturers use the OBIS code 1-0:1.8.0 for the accumulated total active energy.

## Configuration
The communication with the hardware is done using UART. Therefore you need to have an [UART bus](https://esphome.io/components/uart.html#uart) in your configuration with the `rx_pin` connected to the output of your hardware sensor component. The baud rate usually has to be set to 9600bps.

```yaml
# Example configuration entry

external_components:
  - source:
      type: local
      path: components
    components: [ sml ]

uart:
  id: uart_bus
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600
  data_bits: 8
  parity: NONE
  stop_bits: 1

sml:
  id: mysml
  uart_id: uart_bus
  logging: false

sensor:
  - platform: sml
    name: "Total energy"
    sml_id: mysml
    server_id: "0123456789abcdef"
    obis: "1-0:1.8.0"
    unit_of_measurement: kWh
    accuracy_decimals: 1
    filters:
      - multiply: 0.0001

  - platform: sml
    name: "Active power"
    sml_id: mysml
    server_id: "0123456789abcdef"
    obis: "1-0:15.7.0"
    unit_of_measurement: Wh
    accuracy_decimals: 1
    filters:
      - multiply: 0.1

text_sensor:
  - platform: sml
    name: "Manufacturer"
    sml_id: mysml
    server_id: "0123456789abcdef"
    obis: "129-129:199.130.3"
    format: text

  - platform: sml
    name: "Total energy text"
    sml_id: mysml
    server_id: "0123456789abcdef"
    obis: "1-0:1.8.0"
```

## Configuration variables

### SML platform:
- **id** (*Optional*): Specify the ID used for code generation.
- **logging** (*Optional*): If set to `true` a HomeAssistant event (`esphome.sml_obisinfo`) will be fired containing all identified OBIS codes. Defaults to `false`.
- **uart_id** (*Optional*): Manually specify the ID of the [UART Component](https://esphome.io/components/uart.html#uart).

### Sensor
- **obis** (*Required*, string): Specify the OBIS code you want to retrieve data for from the device. The format must be (A-B:C.D.E, e.g. 1-0:1.8.0)
- **server_id** (*Optional*, string): Specify the device's server_id to retrieve the OBIS code from. Should be specified if more then one device is connected to the same hardware sensor component.
- **sml_id** (*Optional*): Specify the ID used for code generation.
- All other options from [Sensor](https://esphome.io/components/sensor/index.html#config-sensor).

### Text Sensor
- **obis** (*Required*, string): Specify the OBIS code you want to retrieve data for from the device. The format must be (A-B:C.D.E, e.g. 1-0:1.8.0)
- **server_id** (*Optional*, string): Specify the device's server_id to retrieve the OBIS code from. Should be specified if more then one device is connected to the same hardware sensor component.
- **format** (*Optional*): Override the automatic interpretation of the binary data value. Possible values (`int`, `uint`, `bool`, `hex`, `text`).
- **sml_id** (*Optional*): Specify the ID used for code generation.
- All other options from [Sensor](https://esphome.io/components/sensor/index.html#config-sensor).


## Obtaining OBIS codes
This component has an integrated logging automatism to obtain information about all transmitted OBIS codes.

For this to work, enable the logging functionality in the `sml` component (`logging: true`). Once enabled, this component will fire a HomeAssistant event with the name `esphome.sml_obisinfo` each time a SML message is received.

To see the contents of the event, go to the developer tools in the HomeAssistant frontend. Switch to the events tab. Here you can insert the `esphome.sml_obisinfo` event in the lower box and start listenig. Once a SML message is received it will be shown in the list which appears below.

## Precision errors
Many smart meters emit very huge numbers for certain OBIS codes (like the accumulated total active energy). This may lead to precision errors for the values reported by the sensor component to ESPHome. This shows in the fact that slightly wrong numbers may be reported to HomeAssistant. This is a result from internal limitations in ESPHome and has nothing to do with the SML component.

If you cannot live with this, you can use the `TextSensor` with an appropriate format to transmit the value as a string to HomeAssistant. On the HomeAssistant side you can define a [Template Sensor](https://www.home-assistant.io/integrations/template/) to cast the value into the appropriate format and do some scaling.

For ESPHome we have:
```yaml
# ESPHome configuration file
text_sensor:
  - platform: sml
    name: "Total energy string"
    obis: "1-0:1.8.0"
    format: uint
```

The `format` parameter is optional. If ommited, the SML component will try to guess the correct datatype from the received SML message.

And in HomeAssistant:
```yaml
# Home Assistant configuration.yaml
template:
  - sensor:
      - name: "Total Energy Consumption"
        unit_of_measurement: "kWh"
        state: "{{ (states('sensor.total_energy_string') | float) * 0.0001 }}"
```
