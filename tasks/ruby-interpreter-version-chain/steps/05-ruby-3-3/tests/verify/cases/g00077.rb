$LOAD_PATH.unshift("/app/gems/lib")
begin
  s = Set.new([1, 1, 2]); t = Set.new([2, 3, 4])
  p s.size
  p (s | t).to_a.sort
  p (s & t).to_a.sort
  p (s - t).to_a.sort
  p s.subset?(Set.new([1, 1, 2] + [2, 3, 4]))
rescue Exception => e
  p e.class
end
