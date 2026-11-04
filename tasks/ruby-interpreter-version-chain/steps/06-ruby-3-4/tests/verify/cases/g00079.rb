$LOAD_PATH.unshift("/app/gems/lib")
require "tsort"
class G
  include TSort
  def initialize(h); @h = h; end
  def tsort_each_node(&b); @h.each_key(&b); end
  def tsort_each_child(n, &b); @h.fetch(n).each(&b); end
end
h = {}
(1..1).each { |i| h[i] = [i % 1 + 1] }
begin
  p G.new(h).tsort
rescue TSort::Cyclic => e
  p e.class
rescue Exception => e
  p e.class
end
