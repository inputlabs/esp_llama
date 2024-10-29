def bin2string(path):
    print(f'Converting {path}')
    array = open(path, 'rb').read()
    string = ''
    for i, byte in enumerate(array):
        string += f"{byte},"
        if not ((i+1) % 32):
            string += '\n    '
    string = string[:-1]
    print(f'  {len(string)} bytes')
    return string


template = open('bin2array/template.c', 'r', encoding='utf8').read()

firmware = bin2string('build/llama.bin')
# bootloader = bin2string('../build/bootloader/bootloader.bin')
# partition = bin2string('../build/partition_table/partition-table.bin')

bin_file = template[:]
bin_file = bin_file.replace('//firmware', firmware)
bin_file = bin_file.replace('//bootloader', '0x00')
bin_file = bin_file.replace('//partition', '0x00')

empty_file = template[:]
empty_file = empty_file.replace('//firmware', '0x00')
empty_file = empty_file.replace('//bootloader', '0x00')
empty_file = empty_file.replace('//partition', '0x00')

bin_file_path = 'build/llama.bin.c'
print(f'Saving {bin_file_path} ({len(bin_file)} bytes)')
open(bin_file_path, 'w', encoding='utf8').write(bin_file)

empty_file_path = 'build/llama_empty.bin.c'
print(f'Saving {empty_file_path} ({len(empty_file)} bytes)')
open(empty_file_path, 'w', encoding='utf8').write(empty_file)
