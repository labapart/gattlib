import Foundation

public typealias dispatch_queue_t = Any

public let CBCentralManagerScanOptionAllowDuplicatesKey = "CBCentralManagerScanOptionAllowDuplicatesKey"
public let CBAdvertisementDataLocalNameKey = "CBAdvertisementDataLocalNameKey"

public enum CBCharacteristicWriteType {
    case withResponse
    case withoutResponse
}

public class CBUUID : Equatable {
    let uuid: UUID?

    public init(string: String) {
        self.uuid = UUID(uuidString: string)
    }

    public static func == (left: CBUUID, right: CBUUID) -> Bool {
        return left.uuid == right.uuid
    }
}

public class CBManager {
    public enum CBManagerState {
        case poweredOn
        case poweredOff
        case unsupported
        case resetting
        case unauthorized
        case unknown
    }
    public var state: CBManagerState

    init(state: CBManagerState) {
        self.state = state
    }
}

public class CBPeer : Equatable {
    public var identifier: UUID

    init(identifier: UUID) {
        self.identifier = identifier
    }

    public static func == (left: CBPeer, right: CBPeer) -> Bool {
        return left.identifier == right.identifier
    }
}

public class CBAttribute {
    public var uuid: CBUUID

    init(uuid: CBUUID) {
        self.uuid = uuid
    }
}

public class CBMutableService {

}

public class CBCentralManager : CBManager {
    init(delegate: (any CBCentralManagerDelegate)?, queue: dispatch_queue_t?, options: [String : Any]?) {
        super.init(state: .unknown)
    }

    func scanForPeripherals(withServices serviceUUIDs: [CBUUID]?, options: [String : Any]? = nil) {

    }

    func stopScan() {

    }

    func connect(_ peripheral: CBPeripheral, options: [String : Any]? = nil) {

    }
}

public protocol CBCentralManagerDelegate {

}

public class CBService : CBAttribute {
    var characteristics: [CBCharacteristic]?
}

public protocol CBPeripheralDelegate {

}

public class CBCharacteristic : CBAttribute {
    var service: CBService?
    var value: Data?
}

public class CBPeripheral : CBPeer {
    var name: String?
    var delegate: (any CBPeripheralDelegate)?
    var services: [CBService]?

    func discoverServices(_ serviceUUIDs: [CBUUID]?) {

    }

    func discoverCharacteristics(_ characteristicUUIDs: [CBUUID]?, for service: CBService) {

    }

    func setNotifyValue(_ enabled: Bool, for characteristic: CBCharacteristic) {

    }

    func writeValue(_ data: Data, for: CBCharacteristic, type: CBCharacteristicWriteType) {

    }

    func maximumWriteValueLength(for type: CBCharacteristicWriteType) -> Int {
        assert(false)
        return -1
    }
}
