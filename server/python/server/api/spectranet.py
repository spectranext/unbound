from typing import Dict, Literal, Union


class SpectranetConfigurationOption:
    def __init__(self, type_: Literal["string", "int", "byte"], value: Union[int, bytes]):
        self.type = type_
        self.value = value


class SpectranetConfiguration:
    def __init__(self, memory):
        self.sections = {}
        self.load(memory)

    @staticmethod
    def get_uint(memory, location):
        return memory[location] + memory[location + 1] * 256

    @staticmethod
    def set_uint(memory, location, value):
        memory[location] = value & 0xFF
        memory[location + 1] = (value & 0xFF00) >> 8

    def obtain_section(self, section_id: int) -> Dict[int, SpectranetConfigurationOption]:
        if section_id in self.sections:
            return self.sections[section_id]

        new_section = {}
        self.sections[section_id] = new_section
        return new_section

    def load(self, memory):
        total_size = self.get_uint(memory, 0)
        pointer = 2

        while pointer < total_size:
            section_id = self.get_uint(memory, pointer)
            pointer += 2
            section_size = self.get_uint(memory, pointer)
            pointer += 2
            new_section = {}

            self.sections[section_id] = new_section

            section_end = pointer + section_size
            while pointer < section_end:
                item_id = memory[pointer]
                pointer += 1

                # The two most significant bits of the config item ID indicate the type
                item_type = item_id & 0xC0

                if item_type == 0x00:  # Null-terminated string
                    string_pointer = pointer
                    while memory[string_pointer]:
                        string_pointer += 1

                    new_section[item_id] = SpectranetConfigurationOption(
                        "string", bytes(memory[pointer:string_pointer]))

                    pointer = string_pointer + 1

                elif item_type == 0x80:  # 8-bit value
                    new_section[item_id] = SpectranetConfigurationOption(
                        "byte", memory[pointer])

                    pointer += 1

                elif item_type == 0xC0:  # 16-bit value
                    new_section[item_id] = SpectranetConfigurationOption(
                        "int", self.get_uint(memory, pointer))
                    pointer += 2

    def bake(self):
        total_size = 2

        for section_id, section in self.sections.items():
            total_size += 4  # section id + section size

            for option_id, option in section.items():
                if option.type == "string":
                    total_size += 2 + len(option.value)
                elif option.type == "byte":
                    total_size += 2
                elif option.type == "int":
                    total_size += 3

        data = bytearray(total_size + 2)
        self.set_uint(data, 0, total_size)

        # terminator
        data[total_size] = 0xFF
        data[total_size + 1] = 0xFF

        pointer = 2

        for section_id, section in self.sections.items():
            self.set_uint(data, pointer, section_id)
            pointer += 2
            section_size_pointer = pointer
            section_size = 0
            pointer += 2

            for option_id, option in section.items():
                data[pointer] = option_id
                pointer += 1

                if option.type == "string":
                    section_size += 2 + len(option.value)
                    data[pointer:pointer + len(option.value)] = option.value
                    pointer += len(option.value)
                    data[pointer] = 0
                    pointer += 1
                elif option.type == "byte":
                    data[pointer] = option.value
                    section_size += 2
                    pointer += 1
                elif option.type == "int":
                    self.set_uint(data, pointer, option.value)
                    section_size += 3
                    pointer += 2

            self.set_uint(data, section_size_pointer, section_size)

        return bytes(data)
