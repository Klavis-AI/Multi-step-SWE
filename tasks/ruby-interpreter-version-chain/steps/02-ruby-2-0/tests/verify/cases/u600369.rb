# frozen_string_literal: false
p(Fiber.new{:ok}.resume)
    p(Fiber.new{
        Fiber.new{
          Fiber.new{
            Fiber.new{
              [:a]
            }.resume + [:b]
          }.resume + [:c]
        }.resume + [:d]
      }.resume + [:e])