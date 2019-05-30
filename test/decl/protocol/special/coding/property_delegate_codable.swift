// RUN: %target-typecheck-verify-swift -verify-ignore-unknown

@_propertyDelegate
struct Wrapper<T: Codable> {
  var value: T
}

@_propertyDelegate
struct WrapperWithInitialValue<T: Codable> {
  var value: T

  init(initialValue: T) {
    self.value = initialValue
  }
}

extension WrapperWithInitialValue: Codable { }

struct X: Codable {
  @WrapperWithInitialValue var foo = 17

  // Make sure the generated key is named 'foo', like the original property.
  private func getFooKey() -> CodingKeys {
    return .foo
  }
}



// expected-error@+2{{type 'Y' does not conform to protocol 'Encodable'}}
// expected-error@+1{{type 'Y' does not conform to protocol 'Decodable'}}
struct Y: Codable {
  @Wrapper var foo: Int
  // expected-note@-1{{cannot automatically synthesize 'Encodable' because 'Wrapper<Int>' does not conform to 'Encodable'}}
  // expected-note@-2{{cannot automatically synthesize 'Decodable' because 'Wrapper<Int>' does not conform to 'Decodable'}}
  
}
