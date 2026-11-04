begin
  a = [1, [2]]
  Ractor.make_shareable(a)
  p a.frozen?
  p a[1].frozen?
  p Ractor.shareable?(a)
rescue Exception => e
  p e.class
end
