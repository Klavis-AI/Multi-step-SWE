begin
  p(({1 => 2, :b => 3}).values)
rescue Exception => e
  p e.class
end
