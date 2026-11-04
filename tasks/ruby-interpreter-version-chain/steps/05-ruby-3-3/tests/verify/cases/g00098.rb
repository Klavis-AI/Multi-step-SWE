$LOAD_PATH.unshift("/app/gems/lib")
begin
  s = Set.new([3, 1, 2])
  p s.to_a.sort_by(&:to_s)
  p s.map { |x| x.to_s }.sort
  p s.include?([3, 1, 2].first)
  p s.frozen?
rescue Exception => e
  p e.class
end
