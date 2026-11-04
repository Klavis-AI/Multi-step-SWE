$LOAD_PATH.unshift("/app/gems/lib")
begin
  s = Set.new([]); t = Set.new([])
  p s.size
  p (s | t).to_a.sort
  p (s & t).to_a.sort
  p (s - t).to_a.sort
  p s.subset?(Set.new([] + []))
rescue Exception => e
  p e.class
end
