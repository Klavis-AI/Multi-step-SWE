begin
  p(({1 => 2, :b => 3}).each_pair { |k, v| })
rescue Exception => e
  p e.class
end
