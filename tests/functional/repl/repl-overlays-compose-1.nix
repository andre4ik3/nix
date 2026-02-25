info: final: prev: {
  var = prev.var + "b";

  # Access the final overlay result even before this is fully computed.
  varUsingFinal = "final value is: " + final.newVar;
}
