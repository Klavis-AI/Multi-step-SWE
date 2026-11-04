begin
  p(({1 => 2, :b => 3}).map { |k, v| [k, v] })
rescue Exception => e
  p e.class
end
