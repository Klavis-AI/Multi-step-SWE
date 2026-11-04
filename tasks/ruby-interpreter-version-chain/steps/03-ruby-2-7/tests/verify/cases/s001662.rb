class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module HashSpecs
  class MyHash < Hash; end

  class MyInitializerHash < Hash

    def initialize
      raise "Constructor called"
    end

  end

  class NewHash < Hash
    def initialize(*args)
      args.each_with_index do |val, index|
        self[index] = val
      end
    end
  end

  class SubHashSettingInInitialize < Hash
    def initialize(*args, &block)
      self[:foo] = :bar
      super(*args, &block)
    end
  end

  class DefaultHash < Hash
    def default(key)
      100
    end
  end

  class ToHashHash < Hash
    def to_hash
      { "to_hash" => "was", "called!" => "duh." }
    end
  end

  class KeyWithPrivateHash
    private :hash
  end

  class ByIdentityKey
    def hash
      fail("#hash should not be called on compare_by_identity Hash")
    end
  end

  class ByValueKey
    attr_reader :n
    def initialize(n)
      @n = n
    end

    def hash
      n
    end

    def eql? other
      ByValueKey === other and @n == other.n
    end
  end

  def self.empty_frozen_hash
    @empty ||= {}
    @empty.freeze
    @empty
  end

  def self.frozen_hash
    @hash ||= { 1 => 2, 3 => 4 }
    @hash.freeze
    @hash
  end
end

h = { a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8, i: 9, j: 10,
          k: 11, l: 12, m: 13, n: 14, o: 15, p: 16, q: 17, r: 18, s: 19, t: 20,
          u: 21, v: 22, w: 23, x: 24, y: 25, z: 26 }
    visited = []
    h.each_pair { |k, v|
      visited << k
      h.delete(k)
    }
    p(visited)
    p(h)