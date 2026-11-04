$LOAD_PATH.unshift("/app/gems/lib")
require "tsort"
class G
  include TSort
  def initialize(h); @h = h; end
  def tsort_each_node(&b); @h.each_key(&b); end
  def tsort_each_child(n, &b); @h.fetch(n).each(&b); end
end
begin
  p G.new({"a" => ["b"], "b" => ["c"], "c" => []}).strongly_connected_components
rescue Exception => e
  p e.class
end
