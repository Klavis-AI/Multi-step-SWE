# frozen_string_literal: false
bug7678 = '[ruby-core:51331]'
    $_ = s1 = "outer"
    s2 = nil
    Fiber.new do
      s2 = "inner"
    end.resume
    p(s2)
    p($_)