# FbMetrics JSON-RPC cURL Commands

## Controller: Activate / Deactivate org.rdk.FbMetrics

### Activate
```bash
curl  -d '{"jsonrpc": "2.0","id":122,"method":"Controller.1.activate","params":{"callsign":"org.rdk.AppGatewayCommon"}}' http://127.0.0.1:9998/jsonrpc
```

### Deactivate
```bash
curl  -d '{"jsonrpc": "2.0","id":122,"method":"Controller.1.deactivate","params":{"callsign":"org.rdk.AppGatewayCommon"}}' http://127.0.0.1:9998/jsonrpc
```

---

## Action
```bash
curl -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":"100","method":"org.rdk.AppGatewayCommon.1.handleAppEventNotifier","params": {"listen": true,"event": "TextToSpeech.Enabled"}}}' http://127.0.0.1:9998/jsonrpc
```


## org.rdk.AppGatewayCommon.1: org.rdk.System callsign-derived aliases

Below are example requests for each of the 14 methods exposed by org.rdk.AppGatewayCommon.1 (methods 1–9 via org.rdk.System; methods 10–14 are Device Branding APIs via IDeviceInfo). 
- For getters/response-only methods, no params are included.
- For methods that require input, an example params object is provided.
- For subscribe methods, the listen parameter is demonstrated.

1) getDeviceMake
- Tests retrieving the device make (maps to org.rdk.System.getDeviceInfo)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"org.rdk.AppGatewayCommon.1.getDeviceMake"}' \
  http://127.0.0.1:9998/jsonrpc
```

2) getDeviceName
- Tests retrieving the device friendly name (maps to org.rdk.System.getFriendlyName)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"org.rdk.AppGatewayCommon.1.getDeviceName"}' \
  http://127.0.0.1:9998/jsonrpc
```

3) setDeviceName
- Tests setting the device friendly name (maps to org.rdk.System.setFriendlyName)
- Example provides a sample name
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"org.rdk.AppGatewayCommon.1.setDeviceName","params":{"name":"Living Room"}}' \
  http://127.0.0.1:9998/jsonrpc
```

4) getDeviceSku
- Tests retrieving the device SKU (maps to org.rdk.System.getSystemVersions)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":4,"method":"org.rdk.AppGatewayCommon.1.getDeviceSku"}' \
  http://127.0.0.1:9998/jsonrpc
```

5) getCountryCode
- Tests retrieving the country code (maps to org.rdk.System.getTerritory -> Firebolt country code)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":5,"method":"org.rdk.AppGatewayCommon.1.getCountryCode"}' \
  http://127.0.0.1:9998/jsonrpc
```

6) setCountryCode
- Tests setting the country code (maps to org.rdk.System.setTerritory)
- Example provides a sample country code (US)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":6,"method":"org.rdk.AppGatewayCommon.1.setCountryCode","params":{"countryCode":"US"}}' \
  http://127.0.0.1:9998/jsonrpc
```

7) getTimeZone
- Tests retrieving the current timezone (maps to org.rdk.System.getTimeZoneDST)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":8,"method":"org.rdk.AppGatewayCommon.1.getTimeZone"}' \
  http://127.0.0.1:9998/jsonrpc
```

8) setTimeZone
- Tests setting the current timezone (maps to org.rdk.System.setTimeZoneDST)
- Example provides an illustrative timezone
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":9,"method":"org.rdk.AppGatewayCommon.1.setTimeZone","params":{"timeZone":"America/New_York"}}' \
  http://127.0.0.1:9998/jsonrpc
```

9) getSecondScreenFriendlyName
- Tests retrieving the second screen friendly name (alias of getDeviceName)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":11,"method":"org.rdk.AppGatewayCommon.1.getSecondScreenFriendlyName"}' \
  http://127.0.0.1:9998/jsonrpc
```

---

## Device Branding APIs (Phase 1) - Firebolt 9.0.0

10) setDeviceOsName
- Sets the operating system name via DeviceInfo.osname (persisted across reboots)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":200,"method":"org.rdk.AppGatewayCommon.1.setDeviceOsName","params":{"value":"RDK-E"}}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":200,"result":null}
```

11) getDeviceOsName
- Reads the operating system name from DeviceInfo.osname
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":201,"method":"org.rdk.AppGatewayCommon.1.getDeviceOsName"}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":201,"result":"RDK-E"}
```

12) setDeviceOsVersion
- Sets the operating system version via DeviceInfo.osversion (persisted across reboots)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":202,"method":"org.rdk.AppGatewayCommon.1.setDeviceOsVersion","params":{"value":"8.3"}}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":202,"result":null}
```

13) getDeviceOsVersion
- Reads the operating system version from DeviceInfo.osversion
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":203,"method":"org.rdk.AppGatewayCommon.1.getDeviceOsVersion"}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":203,"result":"8.3"}
```

14) getDeviceFirmware
- Reads the firmware image name from DeviceInfo.firmwareversion.imagename (GET only)
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":204,"method":"org.rdk.AppGatewayCommon.1.getDeviceFirmware"}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":204,"result":"SKXI11ADS_MIDDLEWARE_DEV_develop_20251101123542"}
```

### Error path: DeviceInfo plugin unavailable
- Deactivate DeviceInfo and confirm error response
```bash
curl -d '{"jsonrpc":"2.0","id":1,"method":"Controller.1.deactivate","params":{"callsign":"DeviceInfo"}}' \
  http://127.0.0.1:9998/jsonrpc

curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":205,"method":"org.rdk.AppGatewayCommon.1.getDeviceOsName"}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: error response (Core::ERROR_UNAVAILABLE)
```

### Invalid SET payload
- Missing 'value' field should return a bad request error
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":206,"method":"org.rdk.AppGatewayCommon.1.setDeviceOsName","params":{}}' \
  http://127.0.0.1:9998/jsonrpc
# Expected: {"jsonrpc":"2.0","id":206,"error":{"code":-32602,"message":"Invalid payload: missing or invalid 'value' field"}}
```
