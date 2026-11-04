begin
  p [1, 2, 3, 4].filter { |x| x.even? }
  p [1, 2, 3, 4].select { |x| x.even? }
rescue Exception => e
  p e.class
end
