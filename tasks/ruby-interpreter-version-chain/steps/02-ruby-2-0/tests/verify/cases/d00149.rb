begin
  proc { |a0, a1, a2, a3| }.call(1)
  p :no_error
rescue ArgumentError => e
  p e.class
end
begin
  lambda { |a0, a1, a2, a3| }.call(1)
  p :no_error
rescue ArgumentError => e
  p e.class
end
