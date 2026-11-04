$LOAD_PATH.unshift("/app/gems/lib")
begin
  s = Set.new([:z, :a, :m])
  p s.to_a.sort_by(&:to_s)
  p s.map { |x| x.to_s }.sort
  p s.include?([:z, :a, :m].first)
  p s.frozen?
rescue Exception => e
  p e.class
end
