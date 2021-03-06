return block
{
      name="cpp_sender",
      meta_data="A sender c-block which can handle cpp data",
      port_cache=true,

      types = {
	-- define types this blocks uses
	 { name="cpp_data", class='struct' },
      },

      configurations= {
	-- add configuration data here. e.g. parameters to set at init
	-- { name="image_width", type_name="int", len=1 },
      },

      ports = {
	-- define the ports for this block
	 { name="output", in_type_name="struct cpp_data", in_data_len=1, doc="output port for cpp data" },
      },
      -- define which operations this block implements
      operations = { start=true, stop=true, step=true },

      cpp = true,
}
