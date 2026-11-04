# -*- encoding: utf-8 -*-
# frozen_string_literal: false
class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
@nfc_normalized_str = "\u1e9b\u0323"
    @nfd_normalized_str = "\u017f\u0323\u0307"
    @nfkc_normalized_str = "\u1e69"
    @nfkd_normalized_str = "\u0073\u0323\u0307"
p(@nfc_normalized_str)
    p(@nfd_normalized_str)